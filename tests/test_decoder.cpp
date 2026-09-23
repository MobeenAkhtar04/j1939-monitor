#include <gtest/gtest.h>

#include "j1939/decoder.hpp"
#include "j1939/frame.hpp"

using namespace j1939;

namespace {
const Signal* find(const std::vector<Signal>& v, uint32_t spn) {
    for (const auto& s : v)
        if (s.spn == spn) return &s;
    return nullptr;
}
}  // namespace

TEST(Decoder, Eec1EngineSpeedAndTorque) {
    // 1500 rpm = 12000 raw = 0x2EE0 (little-endian E0 2E); torque 40 % = 165 raw
    const uint8_t d[8] = {0xF0, 0xFF, 165, 0xE0, 0x2E, 0x00, 0xFF, 0xFF};
    auto s = decode_signals(pgn::EEC1, d, 8);
    ASSERT_NE(find(s, 190), nullptr);
    EXPECT_DOUBLE_EQ(find(s, 190)->value, 1500.0);
    ASSERT_NE(find(s, 513), nullptr);
    EXPECT_DOUBLE_EQ(find(s, 513)->value, 40.0);
}

TEST(Decoder, Et1CoolantUsesMinus40Offset) {
    const uint8_t d[8] = {130, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto s = decode_signals(pgn::ET1, d, 8);
    ASSERT_NE(find(s, 110), nullptr);
    EXPECT_DOUBLE_EQ(find(s, 110)->value, 90.0);
    EXPECT_EQ(find(s, 175), nullptr);  // oil temp is 0xFFFF = not available
}

TEST(Decoder, Ccvs1VehicleSpeed) {
    // 88.5 km/h * 256 = 22656 = 0x5880
    const uint8_t d[8] = {0xFF, 0x80, 0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto s = decode_signals(pgn::CCVS1, d, 8);
    ASSERT_NE(find(s, 84), nullptr);
    EXPECT_DOUBLE_EQ(find(s, 84)->value, 88.5);
}

TEST(Decoder, ErrorAndNotAvailableValuesAreSkipped) {
    const uint8_t na[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_TRUE(decode_signals(pgn::EEC1, na, 8).empty());
    const uint8_t err[8] = {0xFE, 0xFF, 0xFE, 0x00, 0xFE, 0xFF, 0xFF, 0xFF};
    EXPECT_TRUE(decode_signals(pgn::EEC1, err, 8).empty());
    const uint8_t et1_err[8] = {0xFE, 0xFF, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF};  // 0xFEFE = error
    EXPECT_TRUE(decode_signals(pgn::ET1, et1_err, 8).empty());
}

TEST(Decoder, BoundaryRawValues) {
    const uint8_t max_valid[8] = {0xFA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto s = decode_signals(pgn::ET1, max_valid, 8);
    ASSERT_NE(find(s, 110), nullptr);
    EXPECT_DOUBLE_EQ(find(s, 110)->value, 210.0);  // 250 - 40
    const uint8_t min_valid[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_DOUBLE_EQ(find(decode_signals(pgn::ET1, min_valid, 8), 110)->value, -40.0);
}

TEST(Decoder, ShortPayloadDoesNotReadPastEnd) {
    const uint8_t d[3] = {0xF0, 0xFF, 165};
    auto s = decode_signals(pgn::EEC1, d, 3);
    EXPECT_EQ(find(s, 190), nullptr);  // rpm bytes missing
    EXPECT_NE(find(s, 513), nullptr);  // torque present
}

TEST(Decoder, UnknownPgnYieldsNothing) {
    const uint8_t d[8] = {};
    EXPECT_TRUE(decode_signals(0xFEF5, d, 8).empty());
}

TEST(Dm1, NoActiveFaults) {
    const uint8_t d[8] = {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    auto dm1 = decode_dm1(d, 8);
    EXPECT_TRUE(dm1.dtcs.empty());
}

TEST(Dm1, SingleDtc) {
    // SPN 110, FMI 0, OC 3
    const uint8_t d[8] = {0x04, 0xFF, 110, 0x00, 0x00, 0x03, 0xFF, 0xFF};
    auto dm1 = decode_dm1(d, 8);
    ASSERT_EQ(dm1.dtcs.size(), 1u);
    EXPECT_EQ(dm1.dtcs[0].spn, 110u);
    EXPECT_EQ(dm1.dtcs[0].fmi, 0);
    EXPECT_EQ(dm1.dtcs[0].occurrences, 3);
    EXPECT_EQ(dm1.lamp_status, 0x04);
}

TEST(Dm1, NineteenBitSpnUsesHighBitsOfThirdByte) {
    // SPN 524287 (0x7FFFE): low 16 bits 0xFFFE, high 3 bits 0b111 -> 0xE0; FMI 2
    const uint8_t d[6] = {0x00, 0xFF, 0xFE, 0xFF, 0xE2, 0x01};
    auto dm1 = decode_dm1(d, 6);
    ASSERT_EQ(dm1.dtcs.size(), 1u);
    EXPECT_EQ(dm1.dtcs[0].spn, 0x7FFFEu);
    EXPECT_EQ(dm1.dtcs[0].fmi, 2);
}
