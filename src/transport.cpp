#include "j1939/transport.hpp"

namespace j1939 {
namespace {
constexpr uint8_t kControlBam = 32;
constexpr uint16_t kMaxBamSize = 1785;  // 255 packets * 7 bytes
}  // namespace

BamReassembler::BamReassembler(uint64_t timeout_us) : timeout_us_(timeout_us) {}

std::optional<Message> BamReassembler::on_frame(const CanFrame& frame, const Id& id) {
    expire(frame.timestamp_us);
    if (frame.dlc < 8) return std::nullopt;  // TP frames are always 8 bytes
    if (id.pgn == pgn::TP_CM) {
        on_connection_mgmt(frame, id);
        return std::nullopt;
    }
    if (id.pgn == pgn::TP_DT) return on_data(frame, id);
    return std::nullopt;
}

void BamReassembler::on_connection_mgmt(const CanFrame& frame, const Id& id) {
    const auto& d = frame.data;
    // Only BAM (broadcast) sessions; RTS/CTS is out of scope for a passive monitor.
    if (d[0] != kControlBam || id.destination != kGlobalAddress) return;

    const uint16_t size = static_cast<uint16_t>(d[1] | (d[2] << 8));
    const uint8_t packets = d[3];
    const uint32_t target_pgn = d[5] | (d[6] << 8) | ((d[7] & 0x03) << 16);

    const uint16_t expected_packets = static_cast<uint16_t>((size + 6) / 7);
    if (size < 9 || size > kMaxBamSize || packets != expected_packets) {
        ++aborted_;
        sessions_.erase(id.source);
        return;
    }

    // A new BAM from the same source replaces any unfinished one.
    if (sessions_.count(id.source)) ++aborted_;
    Session s;
    s.pgn = target_pgn;
    s.size = size;
    s.packets = packets;
    s.buffer.reserve(static_cast<std::size_t>(packets) * 7);
    s.last_us = frame.timestamp_us;
    sessions_[id.source] = std::move(s);
}

std::optional<Message> BamReassembler::on_data(const CanFrame& frame, const Id& id) {
    auto it = sessions_.find(id.source);
    if (it == sessions_.end()) return std::nullopt;  // data with no announce: ignore
    Session& s = it->second;

    const uint8_t seq = frame.data[0];
    if (seq != s.next_seq) {  // lost or reordered packet: the payload is unusable
        ++aborted_;
        sessions_.erase(it);
        return std::nullopt;
    }

    s.buffer.insert(s.buffer.end(), frame.data.begin() + 1, frame.data.end());
    s.last_us = frame.timestamp_us;
    ++s.next_seq;

    if (seq < s.packets) return std::nullopt;

    Message msg{s.pgn, id.source, std::move(s.buffer)};
    msg.data.resize(s.size);  // drop the 0xFF padding in the last packet
    sessions_.erase(it);
    return msg;
}

void BamReassembler::expire(uint64_t now_us) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now_us > it->second.last_us && now_us - it->second.last_us > timeout_us_) {
            ++aborted_;
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace j1939
