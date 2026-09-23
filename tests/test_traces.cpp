// End-to-end tests: replay recorded candump traces through the full Monitor
// pipeline and check the decoded output and state transitions.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "j1939/io.hpp"
#include "j1939/monitor.hpp"

using namespace j1939;

namespace {

struct Recorder : Sink {
    std::vector<Transition> transitions;
    std::vector<Dm1> dm1s;
    std::size_t signals = 0;
    double max_rpm = 0;
    void on_signal(const Signal& s, uint8_t, uint64_t) override {
        ++signals;
        if (s.spn == 190 && s.value > max_rpm) max_rpm = s.value;
    }
    void on_dm1(const Dm1& d, uint8_t, uint64_t) override { dm1s.push_back(d); }
    void on_transition(const Transition& t) override { transitions.push_back(t); }

    std::vector<State> path() const {
        std::vector<State> p;
        for (const auto& t : transitions) p.push_back(t.to);
        return p;
    }
};

struct Result {
    Recorder rec;
    MonitorStats stats;
    uint64_t aborted = 0;
};

Result replay(const std::string& name) {
    Result r;
    Monitor m;
    m.add_sink(&r.rec);
    LogFileSource src(std::string(TRACE_DIR) + "/" + name, false);
    while (auto f = src.read(0)) m.process(*f);
    r.stats = m.stats();
    r.aborted = m.transport().aborted_sessions();
    return r;
}

}  // namespace

TEST(CandumpFormat, RoundTrips) {
    CanFrame f;
    f.id = 0x18FEEE00;
    f.dlc = 8;
    f.data = {0x6E, 0x00, 0x78, 0x26, 0xFF, 0xFF, 0xFF, 0xFF};
    f.timestamp_us = 1'700'000'000'123'456ULL;
    const std::string line = format_candump_line(f);
    EXPECT_EQ(line, "(1700000000.123456) vcan0 18FEEE00#6E007826FFFFFFFF");
    auto back = parse_candump_line(line);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->id, f.id);
    EXPECT_EQ(back->data, f.data);
    EXPECT_EQ(back->timestamp_us, f.timestamp_us);
}

TEST(CandumpFormat, RejectsStandardIdsAndGarbage) {
    EXPECT_FALSE(parse_candump_line("(1.000000) vcan0 123#0011"));  // 11-bit ID
    EXPECT_FALSE(parse_candump_line("# comment"));
    EXPECT_FALSE(parse_candump_line("(1.000000) vcan0 18FEEE00#6E0"));  // odd hex
}

TEST(Traces, NormalDriveStaysNormal) {
    auto r = replay("normal_drive.log");
    EXPECT_TRUE(r.rec.transitions.empty());
    EXPECT_GT(r.rec.signals, 0u);
    EXPECT_NEAR(r.rec.max_rpm, 1860.0, 5.0);
}

TEST(Traces, OverheatWalksFullStateCycle) {
    auto r = replay("overheat.log");
    const std::vector<State> expected = {State::Warning, State::Fault, State::Recovery, State::Normal};
    EXPECT_EQ(r.rec.path(), expected);
}

TEST(Traces, OverheatDm1ArrivesViaBam) {
    auto r = replay("overheat.log");
    EXPECT_GT(r.stats.bam_completed, 0u);
    bool saw_two = false;
    for (const auto& d : r.rec.dm1s)
        if (d.dtcs.size() == 2 && d.dtcs[0].spn == 110 && d.dtcs[1].spn == 175) saw_two = true;
    EXPECT_TRUE(saw_two);
}

TEST(Traces, CommsLossTriggersStaleFault) {
    auto r = replay("comms_loss.log");
    ASSERT_FALSE(r.rec.transitions.empty());
    EXPECT_EQ(r.rec.transitions.front().to, State::Fault);
    EXPECT_NE(r.rec.transitions.front().reason.find("stale"), std::string::npos);
    EXPECT_EQ(r.rec.path().back(), State::Normal);
}

TEST(Traces, CorruptBamIsDroppedNotMisdecoded) {
    auto r = replay("corrupt_bam.log");
    EXPECT_EQ(r.stats.bam_completed, 0u);
    EXPECT_GE(r.aborted, 1u);
    for (const auto& d : r.rec.dm1s) EXPECT_NE(d.dtcs.size(), 2u);
}

TEST(Traces, FullDriveCycle) {
    auto r = replay("full_cycle.log");
    const std::vector<State> expected = {State::Warning, State::Fault,    State::Recovery, State::Normal,
                                         State::Fault,   State::Recovery, State::Normal};
    EXPECT_EQ(r.rec.path(), expected);
}
