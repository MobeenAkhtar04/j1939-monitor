#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace j1939 {

enum class State { Normal, Warning, Fault, Recovery };
const char* to_string(State s);

struct Thresholds {
    double warning_c = 100.0;             // coolant temp that raises a warning
    double fault_c = 110.0;               // coolant temp that raises a fault
    double clear_c = 95.0;                // must drop below this to leave Warning (hysteresis)
    uint64_t recovery_hold_us = 5'000'000;  // time Recovery must stay clean before Normal
    uint64_t stale_timeout_us = 3'000'000;  // no coolant reading for this long = comms fault
};

struct Transition {
    State from;
    State to;
    std::string reason;
    uint64_t at_us;
};

// Engine-health state machine driven by decoded J1939 data.
//
//   Normal   --(temp >= warning)------------------------> Warning
//   Normal   --(fault condition)------------------------> Fault
//   Warning  --(fault condition)------------------------> Fault
//   Warning  --(temp < clear)---------------------------> Normal
//   Fault    --(no fault condition and temp < warning)--> Recovery
//   Recovery --(fault condition)------------------------> Fault
//   Recovery --(temp >= warning)------------------------> Warning
//   Recovery --(clean for recovery_hold)----------------> Normal
//
// A "fault condition" is any of: temp >= fault threshold, an active DM1 trouble
// code, or coolant data going stale (lost communication with the engine ECU).
//
// Inputs are latched with update_*(); step() evaluates the transition rules once.
class FaultStateMachine {
public:
    explicit FaultStateMachine(Thresholds t = {});

    void update_coolant(double temp_c, uint64_t now_us);
    void update_active_dtcs(std::size_t count);

    // Evaluate the rules at time now_us. Returns the transition taken, if any.
    std::optional<Transition> step(uint64_t now_us);

    State state() const { return state_; }
    const Thresholds& thresholds() const { return t_; }

private:
    std::optional<std::string> fault_reason(uint64_t now_us) const;
    Transition go(State to, std::string reason, uint64_t now_us);

    Thresholds t_;
    State state_ = State::Normal;
    std::optional<double> coolant_c_;
    uint64_t last_coolant_us_ = 0;
    std::size_t active_dtcs_ = 0;
    uint64_t recovery_start_us_ = 0;
};

}  // namespace j1939
