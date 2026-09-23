#pragma once
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "j1939/frame.hpp"

namespace j1939 {

// A complete multi-packet message after reassembly.
struct Message {
    uint32_t pgn;
    uint8_t source;
    std::vector<uint8_t> data;
};

// Reassembles J1939-21 Broadcast Announce Message (BAM) transfers.
//
// Payloads over 8 bytes (e.g. a DM1 with several faults) are sent as:
//   TP.CM  (PGN 60416, control byte 32)  -> announces size, packet count, PGN
//   TP.DT  (PGN 60160) x N               -> 1-byte sequence number + 7 data bytes
//
// One session is tracked per source address. A session is dropped if packets
// arrive out of order or if the gap between packets exceeds the T1 timeout.
// Peer-to-peer RTS/CTS transfers are not handled (this is a passive monitor).
class BamReassembler {
public:
    explicit BamReassembler(uint64_t timeout_us = 750'000);  // J1939-21 T1 = 750 ms

    // Feed every TP.CM / TP.DT frame here. Returns a message once the final packet
    // of a session arrives; otherwise std::nullopt.
    std::optional<Message> on_frame(const CanFrame& frame, const Id& id);

    std::size_t active_sessions() const { return sessions_.size(); }
    uint64_t aborted_sessions() const { return aborted_; }

private:
    struct Session {
        uint32_t pgn = 0;
        uint16_t size = 0;
        uint8_t packets = 0;
        uint8_t next_seq = 1;
        std::vector<uint8_t> buffer;
        uint64_t last_us = 0;
    };

    std::optional<Message> on_data(const CanFrame& frame, const Id& id);
    void on_connection_mgmt(const CanFrame& frame, const Id& id);
    void expire(uint64_t now_us);

    uint64_t timeout_us_;
    uint64_t aborted_ = 0;
    std::unordered_map<uint8_t, Session> sessions_;
};

}  // namespace j1939
