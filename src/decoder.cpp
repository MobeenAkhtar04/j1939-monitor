#include "j1939/decoder.hpp"

#include "j1939/frame.hpp"

namespace j1939 {
namespace {

// J1939 reserves the top of each raw range: 0xFE = error, 0xFF = not available
// (0xFB..0xFD are reserved). Valid data ends at 0xFA / 0xFAFF.
bool valid8(uint8_t raw) { return raw <= 0xFA; }
bool valid16(uint16_t raw) { return raw <= 0xFAFF; }

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void add8(std::vector<Signal>& out, const uint8_t* d, std::size_t len, std::size_t idx,
          uint32_t spn, const char* name, const char* unit, double scale, double offset) {
    if (idx >= len || !valid8(d[idx])) return;
    out.push_back({spn, name, unit, d[idx] * scale + offset});
}

void add16(std::vector<Signal>& out, const uint8_t* d, std::size_t len, std::size_t idx,
           uint32_t spn, const char* name, const char* unit, double scale, double offset) {
    if (idx + 1 >= len) return;
    const uint16_t raw = le16(d + idx);
    if (!valid16(raw)) return;
    out.push_back({spn, name, unit, raw * scale + offset});
}

}  // namespace

// Byte positions below are 0-based; the J1939-71 spec numbers bytes from 1.
std::vector<Signal> decode_signals(uint32_t pgn_value, const uint8_t* d, std::size_t len) {
    std::vector<Signal> out;
    switch (pgn_value) {
        case pgn::EEC1:
            add8(out, d, len, 2, 513, "Actual Engine Torque", "%", 1.0, -125.0);
            add16(out, d, len, 3, 190, "Engine Speed", "rpm", 0.125, 0.0);
            break;
        case pgn::ET1:
            add8(out, d, len, 0, 110, "Engine Coolant Temperature", "degC", 1.0, -40.0);
            add16(out, d, len, 2, 175, "Engine Oil Temperature", "degC", 0.03125, -273.0);
            break;
        case pgn::CCVS1:
            add16(out, d, len, 1, 84, "Wheel-Based Vehicle Speed", "km/h", 1.0 / 256.0, 0.0);
            break;
        default:
            break;
    }
    return out;
}

Dm1 decode_dm1(const uint8_t* d, std::size_t len) {
    Dm1 dm1;
    if (len < 2) return dm1;
    dm1.lamp_status = d[0];
    // Each DTC is 4 bytes, starting at byte 3 (index 2).
    for (std::size_t i = 2; i + 3 < len; i += 4) {
        const uint32_t spn = static_cast<uint32_t>(d[i]) |
                             (static_cast<uint32_t>(d[i + 1]) << 8) |
                             (static_cast<uint32_t>(d[i + 2] & 0xE0) << 11);
        const uint8_t fmi = d[i + 2] & 0x1F;
        const uint8_t oc = d[i + 3] & 0x7F;
        // SPN 0 / FMI 0 is how a DM1 says "no active faults".
        if (spn == 0 && fmi == 0) continue;
        // Unused DTC slots in a padded frame are 0xFF-filled.
        if (spn == 0x7FFFF && fmi == 0x1F) continue;
        dm1.dtcs.push_back({spn, fmi, oc});
    }
    return dm1;
}

}  // namespace j1939
