#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace j1939 {

// One decoded Suspect Parameter Number (SPN) in engineering units.
struct Signal {
    uint32_t spn;
    std::string name;
    std::string unit;
    double value;
};

// One Diagnostic Trouble Code from a DM1 message.
struct Dtc {
    uint32_t spn;         // which parameter is faulty (19 bits)
    uint8_t fmi;          // Failure Mode Identifier (5 bits)
    uint8_t occurrences;  // occurrence count (7 bits)
};

struct Dm1 {
    uint8_t lamp_status = 0;  // byte 1: MIL / red stop / amber warning / protect lamps
    std::vector<Dtc> dtcs;    // active faults (empty if none)
};

// Decode the SPNs this project supports from a PGN payload.
// Parameters whose raw value is in the J1939 "error" or "not available"
// range are skipped rather than reported as bogus numbers.
std::vector<Signal> decode_signals(uint32_t pgn, const uint8_t* data, std::size_t len);

// Decode a DM1 payload (single frame or reassembled multi-packet).
Dm1 decode_dm1(const uint8_t* data, std::size_t len);

}  // namespace j1939
