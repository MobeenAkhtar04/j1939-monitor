#include "j1939/fault_state_machine.hpp"

#include <cstdio>

namespace j1939 {

const char* to_string(State s) {
    switch (s) {
        case State::Normal: return "Normal";
        case State::Warning: return "Warning";
        case State::Fault: return "Fault";
        case State::Recovery: return "Recovery";
    }
    return "Unknown";
}

FaultStateMachine::FaultStateMachine(Thresholds t) : t_(t) {}

void FaultStateMachine::update_coolant(double temp_c, uint64_t now_us) {
    coolant_c_ = temp_c;
    last_coolant_us_ = now_us;
}

void FaultStateMachine::update_active_dtcs(std::size_t count) { active_dtcs_ = count; }

std::optional<std::string> FaultStateMachine::fault_reason(uint64_t now_us) const {
    char buf[96];
    if (coolant_c_ && now_us > last_coolant_us_ &&
        now_us - last_coolant_us_ > t_.stale_timeout_us) {
        return std::string("coolant data stale (lost ECU communication)");
    }
    if (active_dtcs_ > 0) {
        std::snprintf(buf, sizeof buf, "%zu active DTC(s) in DM1", active_dtcs_);
        return std::string(buf);
    }
    if (coolant_c_ && *coolant_c_ >= t_.fault_c) {
        std::snprintf(buf, sizeof buf, "coolant %.1f C >= fault limit %.1f C", *coolant_c_,
                      t_.fault_c);
        return std::string(buf);
    }
    return std::nullopt;
}

Transition FaultStateMachine::go(State to, std::string reason, uint64_t now_us) {
    Transition tr{state_, to, std::move(reason), now_us};
    state_ = to;
    if (to == State::Recovery) recovery_start_us_ = now_us;
    return tr;
}

std::optional<Transition> FaultStateMachine::step(uint64_t now_us) {
    const auto fault = fault_reason(now_us);
    const bool warm = coolant_c_ && *coolant_c_ >= t_.warning_c;
    const bool cleared = coolant_c_ && *coolant_c_ < t_.clear_c;
    char buf[96];
    if (coolant_c_) std::snprintf(buf, sizeof buf, "coolant %.1f C", *coolant_c_);

    switch (state_) {
        case State::Normal:
            if (fault) return go(State::Fault, *fault, now_us);
            if (warm) return go(State::Warning, std::string(buf) + " >= warning limit", now_us);
            break;
        case State::Warning:
            if (fault) return go(State::Fault, *fault, now_us);
            if (cleared) return go(State::Normal, std::string(buf) + " below clear limit", now_us);
            break;
        case State::Fault:
            if (!fault && !warm) return go(State::Recovery, "fault conditions cleared", now_us);
            break;
        case State::Recovery:
            if (fault) return go(State::Fault, *fault, now_us);
            if (warm) return go(State::Warning, std::string(buf) + " >= warning limit", now_us);
            if (now_us - recovery_start_us_ >= t_.recovery_hold_us)
                return go(State::Normal, "stable for recovery hold period", now_us);
            break;
    }
    return std::nullopt;
}

}  // namespace j1939
