#include <gtest/gtest.h>

#include "j1939/frame.hpp"

using namespace j1939;

TEST(Id, ParsesPdu2BroadcastEt1) {
    // 0x18FEEE00: priority 6, PGN 65262 (ET1), from engine (SA 0x00)
    const Id id = parse_id(0x18FEEE00);
    EXPECT_EQ(id.priority, 6);
    EXPECT_EQ(id.pgn, pgn::ET1);
    EXPECT_EQ(id.source, 0x00);
    EXPECT_EQ(id.destination, kGlobalAddress);
}

TEST(Id, ParsesEec1) {
    const Id id = parse_id(0x0CF00400);
    EXPECT_EQ(id.priority, 3);
    EXPECT_EQ(id.pgn, pgn::EEC1);
}

TEST(Id, Pdu1DestinationIsNotPartOfPgn) {
    // TP.CM (PF 0xEC) sent from 0x17 to 0x00
    const Id id = parse_id(0x1CEC0017);
    EXPECT_EQ(id.priority, 7);
    EXPECT_EQ(id.pgn, pgn::TP_CM);
    EXPECT_EQ(id.destination, 0x00);
    EXPECT_EQ(id.source, 0x17);
}

TEST(Id, DataPageBitIsPartOfPgn) {
    const Id id = parse_id(0x19FEEE00);  // DP=1
    EXPECT_EQ(id.pgn, 0x1FEEEu);
}

TEST(Id, MakeIdRoundTrips) {
    for (uint32_t p : {pgn::EEC1, pgn::ET1, pgn::CCVS1, pgn::DM1}) {
        const Id id = parse_id(make_id(6, p, 0x21));
        EXPECT_EQ(id.pgn, p);
        EXPECT_EQ(id.source, 0x21);
        EXPECT_EQ(id.priority, 6);
    }
    const Id tp = parse_id(make_id(7, pgn::TP_DT, 0x00, 0x42));
    EXPECT_EQ(tp.pgn, pgn::TP_DT);
    EXPECT_EQ(tp.destination, 0x42);
}
