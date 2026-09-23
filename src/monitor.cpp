#include "j1939/monitor.hpp"

#include "j1939/decoder.hpp"

namespace j1939 {

Monitor::Monitor(Thresholds thresholds) : fsm_(thresholds) {}

void Monitor::process(const CanFrame& frame) {
    ++stats_.frames;
    const Id id = parse_id(frame.id);

    if (id.pgn == pgn::TP_CM || id.pgn == pgn::TP_DT) {
        if (auto msg = bam_.on_frame(frame, id)) {
            ++stats_.bam_completed;
            handle_message(msg->pgn, msg->source, msg->data.data(), msg->data.size(),
                           frame.timestamp_us);
        }
    } else {
        handle_message(id.pgn, id.source, frame.data.data(), frame.dlc, frame.timestamp_us);
    }
    run_state_machine(frame.timestamp_us);
}

void Monitor::tick(uint64_t now_us) { run_state_machine(now_us); }

void Monitor::handle_message(uint32_t pgn_value, uint8_t source, const uint8_t* data,
                             std::size_t len, uint64_t t_us) {
    if (pgn_value == pgn::DM1) {
        const Dm1 dm1 = decode_dm1(data, len);
        ++stats_.decoded_messages;
        fsm_.update_active_dtcs(dm1.dtcs.size());
        for (Sink* s : sinks_) s->on_dm1(dm1, source, t_us);
        return;
    }

    const auto signals = decode_signals(pgn_value, data, len);
    if (signals.empty()) {
        ++stats_.ignored;
        return;
    }
    ++stats_.decoded_messages;
    for (const Signal& sig : signals) {
        if (sig.spn == 110) fsm_.update_coolant(sig.value, t_us);
        for (Sink* s : sinks_) s->on_signal(sig, source, t_us);
    }
}

void Monitor::run_state_machine(uint64_t now_us) {
    // Loop in case one input change justifies more than one hop
    // (bounded so a bad rule can never spin forever).
    for (int i = 0; i < 4; ++i) {
        auto tr = fsm_.step(now_us);
        if (!tr) break;
        for (Sink* s : sinks_) s->on_transition(*tr);
    }
}

}  // namespace j1939
