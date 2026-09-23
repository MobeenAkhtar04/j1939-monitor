#pragma once
#include <cstdint>
#include <vector>

#include "j1939/fault_state_machine.hpp"
#include "j1939/frame.hpp"
#include "j1939/sinks.hpp"
#include "j1939/transport.hpp"

namespace j1939 {

struct MonitorStats {
    uint64_t frames = 0;           // all frames seen
    uint64_t decoded_messages = 0; // single-frame + reassembled messages we understood
    uint64_t bam_completed = 0;    // multi-packet messages reassembled
    uint64_t ignored = 0;          // PGNs this monitor does not decode
};

// The decode pipeline:  CanFrame -> Id -> (BAM reassembly) -> decoder -> state machine -> sinks
class Monitor {
public:
    explicit Monitor(Thresholds thresholds = {});

    void add_sink(Sink* sink) { sinks_.push_back(sink); }

    // Process one received frame. Uses frame.timestamp_us as "now".
    void process(const CanFrame& frame);

    // Re-evaluate time-based rules (stale data, recovery hold) when no frames arrive.
    void tick(uint64_t now_us);

    const MonitorStats& stats() const { return stats_; }
    const FaultStateMachine& state_machine() const { return fsm_; }
    const BamReassembler& transport() const { return bam_; }

private:
    void handle_message(uint32_t pgn, uint8_t source, const uint8_t* data, std::size_t len,
                        uint64_t t_us);
    void run_state_machine(uint64_t now_us);

    FaultStateMachine fsm_;
    BamReassembler bam_;
    std::vector<Sink*> sinks_;
    MonitorStats stats_;
};

}  // namespace j1939
