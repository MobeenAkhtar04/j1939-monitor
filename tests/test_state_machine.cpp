#include <gtest/gtest.h>

#include "j1939/fault_state_machine.hpp"

using namespace j1939;

namespace {
constexpr uint64_t S = 1'000'000;  // one second in microseconds

State drive(FaultStateMachine& m, double temp, uint64_t t) {
    m.update_coolant(temp, t);
    m.step(t);
    return m.state();
}
}  // namespace

TEST(Fsm, StartsNormal) {
    FaultStateMachine m;
    EXPECT_EQ(m.state(), State::Normal);
    EXPECT_FALSE(m.step(0));
}

TEST(Fsm, NormalToWarningAtThreshold) {
    FaultStateMachine m;
    EXPECT_EQ(drive(m, 99.0, 1 * S), State::Normal);
    EXPECT_EQ(drive(m, 100.0, 2 * S), State::Warning);
}

TEST(Fsm, WarningUsesHysteresis) {
    FaultStateMachine m;
    drive(m, 101.0, 1 * S);
    EXPECT_EQ(drive(m, 97.0, 2 * S), State::Warning);  // below warning but above clear
    EXPECT_EQ(drive(m, 94.0, 3 * S), State::Normal);
}

TEST(Fsm, OverTemperatureIsFault) {
    FaultStateMachine m;
    drive(m, 105.0, 1 * S);
    EXPECT_EQ(drive(m, 110.0, 2 * S), State::Fault);
}

TEST(Fsm, NormalCanJumpStraightToFault) {
    FaultStateMachine m;
    EXPECT_EQ(drive(m, 115.0, 1 * S), State::Fault);
}

TEST(Fsm, ActiveDtcIsFaultEvenAtNormalTemp) {
    FaultStateMachine m;
    m.update_coolant(80.0, 1 * S);
    m.update_active_dtcs(1);
    auto tr = m.step(1 * S);
    ASSERT_TRUE(tr);
    EXPECT_EQ(tr->to, State::Fault);
    EXPECT_NE(tr->reason.find("DTC"), std::string::npos);
}

TEST(Fsm, FaultGoesThroughRecoveryBeforeNormal) {
    FaultStateMachine m;
    drive(m, 112.0, 1 * S);
    EXPECT_EQ(drive(m, 104.0, 2 * S), State::Fault);     // still above warning
    EXPECT_EQ(drive(m, 90.0, 3 * S), State::Recovery);
    EXPECT_EQ(drive(m, 90.0, 5 * S), State::Recovery);   // hold not yet elapsed
    EXPECT_EQ(drive(m, 90.0, 8 * S), State::Normal);     // 5 s hold elapsed
}

TEST(Fsm, RelapseDuringRecoveryReturnsToFault) {
    FaultStateMachine m;
    drive(m, 112.0, 1 * S);
    drive(m, 90.0, 2 * S);
    EXPECT_EQ(m.state(), State::Recovery);
    EXPECT_EQ(drive(m, 111.0, 3 * S), State::Fault);
}

TEST(Fsm, WarmDuringRecoveryGoesToWarning) {
    FaultStateMachine m;
    drive(m, 112.0, 1 * S);
    drive(m, 90.0, 2 * S);
    EXPECT_EQ(drive(m, 101.0, 3 * S), State::Warning);
}

TEST(Fsm, StaleCoolantDataIsFault) {
    FaultStateMachine m;  // stale timeout 3 s
    drive(m, 85.0, 1 * S);
    EXPECT_FALSE(m.step(3 * S));
    auto tr = m.step(4 * S + 1);
    ASSERT_TRUE(tr);
    EXPECT_EQ(tr->to, State::Fault);
    EXPECT_NE(tr->reason.find("stale"), std::string::npos);
    EXPECT_EQ(drive(m, 85.0, 5 * S), State::Recovery);  // data resumes
}

TEST(Fsm, NoDataYetIsNotStale) {
    FaultStateMachine m;
    EXPECT_FALSE(m.step(100 * S));
    EXPECT_EQ(m.state(), State::Normal);
}
