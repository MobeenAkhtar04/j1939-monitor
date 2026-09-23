// j1939_sim: generates realistic J1939 engine traffic for a scripted drive cycle
// (warm-up, overheat with DM1 faults, cool-down, lost ECU communication) and
// sends it to a SocketCAN interface or writes it to a candump -L trace file.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "j1939/frame.hpp"
#include "j1939/io.hpp"

using namespace j1939;

namespace {

constexpr uint8_t kEngineSa = 0x00;  // engine #1 source address

// ---- drive-cycle profile (t in seconds) --------------------------------------
double rpm_at(double t) {
    if (t < 3) return 800.0 * t / 3.0;
    if (t < 10) return 800.0 + 1000.0 * (t - 3) / 7.0;
    return 1800.0 + 60.0 * std::sin(t);
}
double speed_at(double t) {
    if (t < 5) return 0.0;
    if (t < 15) return 90.0 * (t - 5) / 10.0;
    return 90.0 + 2.0 * std::sin(t / 2.0);
}
double coolant_at(double t) {
    if (t < 8) return 70.0;
    if (t < 29) return 70.0 + 2.0 * (t - 8);    // climbs: warning at ~23 s, fault at ~28 s
    if (t < 31) return 112.0;
    if (t < 40) return 112.0 - 3.0 * (t - 31);  // cooling system catches up
    return 85.0;
}
bool dtcs_active(double t) { return t >= 28.0 && t < 33.0; }
bool ecu_silent(double t) { return t >= 44.0 && t < 48.0; }  // simulated comms loss (ET1 missing)

// ---- frame builders ------------------------------------------------------------
CanFrame frame(uint8_t prio, uint32_t pgn_value, std::initializer_list<uint8_t> bytes, uint8_t dest = kGlobalAddress) {
    CanFrame f;
    f.id = make_id(prio, pgn_value, kEngineSa, dest);
    f.dlc = 8;
    f.data.fill(0xFF);
    std::copy(bytes.begin(), bytes.end(), f.data.begin());
    return f;
}

CanFrame eec1(double rpm) {
    const auto raw = static_cast<uint16_t>(rpm / 0.125);
    const auto torque = static_cast<uint8_t>(125 + 20 + rpm / 100.0);  // offset -125 %
    return frame(3, pgn::EEC1, {0xF0, 0xFF, torque, uint8_t(raw & 0xFF), uint8_t(raw >> 8), kEngineSa});
}
CanFrame et1(double coolant) {
    const auto oil = static_cast<uint16_t>((coolant + 10.0 + 273.0) / 0.03125);
    return frame(6, pgn::ET1, {uint8_t(coolant + 40.0), 0xFF, uint8_t(oil & 0xFF), uint8_t(oil >> 8)});
}
CanFrame ccvs1(double kmh) {
    const auto raw = static_cast<uint16_t>(kmh * 256.0);
    return frame(6, pgn::CCVS1, {0xFF, uint8_t(raw & 0xFF), uint8_t(raw >> 8)});
}

void put_dtc(std::vector<uint8_t>& out, uint32_t spn, uint8_t fmi, uint8_t oc) {
    out.push_back(uint8_t(spn & 0xFF));
    out.push_back(uint8_t((spn >> 8) & 0xFF));
    out.push_back(uint8_t(((spn >> 11) & 0xE0) | (fmi & 0x1F)));
    out.push_back(uint8_t(oc & 0x7F));
}

// DM1 with no faults fits in one frame. Two or more DTCs need >8 bytes, so they
// go out as a BAM: one TP.CM announce followed by TP.DT packets.
std::vector<CanFrame> dm1(bool faults) {
    if (!faults) return {frame(6, pgn::DM1, {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00})};

    std::vector<uint8_t> payload = {0x14, 0xFF};  // amber warning + red stop lamps on
    put_dtc(payload, 110, 0, 1);                  // coolant temp: above normal, most severe
    put_dtc(payload, 175, 16, 1);                 // oil temp: above normal, moderately severe

    const auto size = static_cast<uint16_t>(payload.size());
    const auto packets = static_cast<uint8_t>((size + 6) / 7);
    std::vector<CanFrame> out;
    out.push_back(frame(7, pgn::TP_CM, {32, uint8_t(size & 0xFF), uint8_t(size >> 8), packets, 0xFF,
                                        uint8_t(pgn::DM1 & 0xFF), uint8_t((pgn::DM1 >> 8) & 0xFF),
                                        uint8_t(pgn::DM1 >> 16)}));
    for (uint8_t seq = 1; seq <= packets; ++seq) {
        CanFrame dt = frame(7, pgn::TP_DT, {seq});
        for (int i = 0; i < 7; ++i) {
            const std::size_t idx = (seq - 1) * 7u + i;
            dt.data[1 + i] = idx < payload.size() ? payload[idx] : 0xFF;
        }
        out.push_back(dt);
    }
    return out;
}

// ---- output -----------------------------------------------------------------------
struct Output {
    std::unique_ptr<SocketCanWriter> can;
    std::ofstream file;
    uint64_t sent = 0;

    void send(CanFrame f, uint64_t t_us) {
        f.timestamp_us = t_us;
        if (can) {
            while (!can->write(f)) {  // ENOBUFS: tx queue full, back off briefly
                if (errno != ENOBUFS) throw std::runtime_error("CAN write failed");
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        } else {
            file << format_candump_line(f) << '\n';
        }
        ++sent;
    }
};

void usage() {
    std::puts(
        "usage: j1939_sim (--iface IFACE | --out FILE) [--duration SEC] [--loop] [--flood N]\n\n"
        "  --iface vcan0     send frames to a SocketCAN interface in real time\n"
        "  --out trace.log   write a candump -L trace instead\n"
        "  --duration 55     length of the drive cycle in seconds (default 55)\n"
        "  --loop            repeat the drive cycle until Ctrl+C (live mode only)\n"
        "  --flood N         stress test: send N EEC1 frames back-to-back, then exit");
}

}  // namespace

int main(int argc, char** argv) {
    std::string iface, out_path;
    double duration = 55.0;
    bool loop = false;
    long flood = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : "0"; };
        if (a == "--iface") iface = next();
        else if (a == "--out") out_path = next();
        else if (a == "--duration") duration = std::stod(next());
        else if (a == "--loop") loop = true;
        else if (a == "--flood") flood = std::stol(next());
        else { usage(); return a == "--help" ? 0 : 2; }
    }
    if (iface.empty() == out_path.empty()) { usage(); return 2; }

    try {
        Output out;
        if (!iface.empty()) out.can = std::make_unique<SocketCanWriter>(iface);
        else out.file.open(out_path);

        const bool live = !iface.empty();
        const uint64_t epoch = live ? monotonic_us() : 1'700'000'000'000'000ULL;

        if (flood > 0) {
            const uint64_t t0 = monotonic_us();
            for (long n = 0; n < flood; ++n) out.send(eec1(1500.0 + (n % 400)), epoch + n);
            const double secs = (monotonic_us() - t0) / 1e6;
            std::printf("flood: sent %ld frames in %.3f s (%.0f frames/s)\n", flood, secs, flood / secs);
            return 0;
        }

        do {
            const uint64_t start = live ? monotonic_us() : epoch;
            std::deque<std::pair<uint64_t, CanFrame>> pending;  // queued BAM packets
            const uint64_t step = 10'000;                        // 10 ms scheduler tick
            for (uint64_t t_us = 0; t_us <= static_cast<uint64_t>(duration * 1e6); t_us += step) {
                const double t = t_us / 1e6;
                const uint64_t now = start + t_us;
                if (t_us % 20'000 == 0) out.send(eec1(rpm_at(t)), now);        // 20 ms
                if (t_us % 100'000 == 0) out.send(ccvs1(speed_at(t)), now);    // 100 ms
                if (t_us % 1'000'000 == 0 && !ecu_silent(t)) out.send(et1(coolant_at(t)), now);  // 1 s
                if (t_us % 1'000'000 == 500'000) {                              // 1 s, offset
                    auto frames = dm1(dtcs_active(t));
                    for (std::size_t i = 0; i < frames.size(); ++i)
                        pending.emplace_back(now + i * 50'000, frames[i]);      // 50 ms between TP packets
                }
                while (!pending.empty() && pending.front().first <= now) {
                    out.send(pending.front().second, now);
                    pending.pop_front();
                }
                if (live) {
                    const uint64_t due = start + t_us + step;
                    const uint64_t cur = monotonic_us();
                    if (due > cur) std::this_thread::sleep_for(std::chrono::microseconds(due - cur));
                }
            }
            std::printf("drive cycle complete: %llu frames sent\n", (unsigned long long)out.sent);
        } while (loop && live);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
