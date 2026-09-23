#include "j1939/frame.hpp"

namespace j1939 {

// 29-bit layout: | P(3) | EDP(1) | DP(1) | PF(8) | PS(8) | SA(8) |
Id parse_id(uint32_t can_id) {
    Id id;
    id.priority = static_cast<uint8_t>((can_id >> 26) & 0x7);
    const uint32_t edp_dp = (can_id >> 24) & 0x3;
    const uint8_t pf = static_cast<uint8_t>((can_id >> 16) & 0xFF);
    const uint8_t ps = static_cast<uint8_t>((can_id >> 8) & 0xFF);
    id.source = static_cast<uint8_t>(can_id & 0xFF);

    if (pf < 240) {
        // PDU1: PS is the destination address, not part of the PGN.
        id.pgn = (edp_dp << 16) | (static_cast<uint32_t>(pf) << 8);
        id.destination = ps;
    } else {
        // PDU2: PS is the group extension and is part of the PGN.
        id.pgn = (edp_dp << 16) | (static_cast<uint32_t>(pf) << 8) | ps;
        id.destination = kGlobalAddress;
    }
    return id;
}

uint32_t make_id(uint8_t priority, uint32_t pgn, uint8_t source, uint8_t destination) {
    const uint8_t pf = static_cast<uint8_t>((pgn >> 8) & 0xFF);
    const uint8_t ps = pf < 240 ? destination : static_cast<uint8_t>(pgn & 0xFF);
    return (static_cast<uint32_t>(priority & 0x7) << 26) |
           ((pgn & 0x3FF00u) << 8) |
           (static_cast<uint32_t>(ps) << 8) |
           source;
}

}  // namespace j1939
