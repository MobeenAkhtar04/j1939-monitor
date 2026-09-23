#include <gtest/gtest.h>

#include "j1939/transport.hpp"

using namespace j1939;

namespace {

CanFrame tp(uint32_t pgn_value, std::array<uint8_t, 8> data, uint64_t t_us, uint8_t sa = 0x00) {
    CanFrame f;
    f.id = make_id(7, pgn_value, sa, kGlobalAddress);
    f.dlc = 8;
    f.data = data;
    f.timestamp_us = t_us;
    return f;
}

// BAM announce for a 10-byte DM1 in 2 packets.
CanFrame announce(uint64_t t, uint8_t sa = 0x00) {
    return tp(pgn::TP_CM, {32, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00}, t, sa);
}
CanFrame packet1(uint64_t t, uint8_t sa = 0x00) {
    return tp(pgn::TP_DT, {1, 0x14, 0xFF, 110, 0x00, 0x00, 0x01, 175}, t, sa);
}
CanFrame packet2(uint64_t t, uint8_t sa = 0x00) {
    return tp(pgn::TP_DT, {2, 0x00, 0x10, 0x01, 0xFF, 0xFF, 0xFF, 0xFF}, t, sa);
}

std::optional<Message> feed(BamReassembler& r, const CanFrame& f) { return r.on_frame(f, parse_id(f.id)); }

}  // namespace

TEST(Bam, ReassemblesTwoPacketDm1) {
    BamReassembler r;
    EXPECT_FALSE(feed(r, announce(0)));
    EXPECT_FALSE(feed(r, packet1(50'000)));
    auto msg = feed(r, packet2(100'000));
    ASSERT_TRUE(msg);
    EXPECT_EQ(msg->pgn, pgn::DM1);
    EXPECT_EQ(msg->source, 0x00);
    ASSERT_EQ(msg->data.size(), 10u);  // padding trimmed
    EXPECT_EQ(msg->data[0], 0x14);
    EXPECT_EQ(msg->data[6], 175);
    EXPECT_EQ(msg->data[9], 0x01);
    EXPECT_EQ(r.active_sessions(), 0u);
}

TEST(Bam, OutOfOrderPacketAbortsSession) {
    BamReassembler r;
    feed(r, announce(0));
    EXPECT_FALSE(feed(r, packet2(50'000)));
    EXPECT_EQ(r.aborted_sessions(), 1u);
    EXPECT_FALSE(feed(r, packet1(100'000)));  // session is gone
}

TEST(Bam, TimeoutBetweenPacketsAbortsSession) {
    BamReassembler r(750'000);
    feed(r, announce(0));
    feed(r, packet1(50'000));
    EXPECT_FALSE(feed(r, packet2(1'000'000)));  // 950 ms gap > T1
    EXPECT_EQ(r.aborted_sessions(), 1u);
}

TEST(Bam, DataWithoutAnnounceIsIgnored) {
    BamReassembler r;
    EXPECT_FALSE(feed(r, packet1(0)));
    EXPECT_EQ(r.active_sessions(), 0u);
}

TEST(Bam, InconsistentAnnounceIsRejected) {
    BamReassembler r;
    // claims 10 bytes but 5 packets
    feed(r, tp(pgn::TP_CM, {32, 10, 0, 5, 0xFF, 0xCA, 0xFE, 0x00}, 0));
    EXPECT_EQ(r.active_sessions(), 0u);
    EXPECT_EQ(r.aborted_sessions(), 1u);
}

TEST(Bam, SessionsFromDifferentSourcesAreIndependent) {
    BamReassembler r;
    feed(r, announce(0, 0x00));
    feed(r, announce(0, 0x03));
    feed(r, packet1(10'000, 0x03));
    feed(r, packet1(20'000, 0x00));
    EXPECT_TRUE(feed(r, packet2(30'000, 0x03)));
    EXPECT_TRUE(feed(r, packet2(40'000, 0x00)));
}

TEST(Bam, RtsCtsIsIgnoredByPassiveMonitor) {
    BamReassembler r;
    CanFrame rts;
    rts.id = make_id(7, pgn::TP_CM, 0x00, 0x21);  // destination-specific
    rts.dlc = 8;
    rts.data = {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00};
    feed(r, rts);
    EXPECT_EQ(r.active_sessions(), 0u);
}
