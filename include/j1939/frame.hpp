#pragma once
#include <array>
#include <cstdint>

namespace j1939 {

// A raw CAN 2.0B frame as seen on the bus. J1939 only uses 29-bit extended IDs.
struct CanFrame {
    uint32_t id = 0;                  // 29-bit identifier (no EFF/RTR flags)
    uint8_t dlc = 0;                  // payload length, 0..8
    std::array<uint8_t, 8> data{};    // payload
    uint64_t timestamp_us = 0;        // receive time (monotonic, microseconds)
};

// Fields packed into a 29-bit J1939 identifier.
struct Id {
    uint8_t priority = 0;             // 0 (highest) .. 7
    uint32_t pgn = 0;                 // Parameter Group Number (18 bits)
    uint8_t source = 0;               // source address
    uint8_t destination = 0xFF;       // 0xFF = global (broadcast)
};

constexpr uint8_t kGlobalAddress = 0xFF;

// Well-known PGNs used by this project.
namespace pgn {
constexpr uint32_t EEC1 = 61444;      // Electronic Engine Controller 1   (0xF004)
constexpr uint32_t ET1 = 65262;       // Engine Temperature 1             (0xFEEE)
constexpr uint32_t CCVS1 = 65265;     // Cruise Control/Vehicle Speed 1   (0xFEF1)
constexpr uint32_t DM1 = 65226;       // Active Diagnostic Trouble Codes  (0xFECA)
constexpr uint32_t TP_CM = 60416;     // Transport Protocol - Conn. Mgmt  (0xEC00)
constexpr uint32_t TP_DT = 60160;     // Transport Protocol - Data Xfer   (0xEB00)
}  // namespace pgn

// Decode a 29-bit identifier. Handles PDU1 (destination-specific, PF < 240)
// and PDU2 (broadcast, PF >= 240) formats.
Id parse_id(uint32_t can_id);

// Build a 29-bit identifier. For PDU1 PGNs the destination goes into the PS byte;
// for PDU2 PGNs the destination argument is ignored.
uint32_t make_id(uint8_t priority, uint32_t pgn, uint8_t source,
                 uint8_t destination = kGlobalAddress);

}  // namespace j1939
