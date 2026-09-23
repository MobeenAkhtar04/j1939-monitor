// j1939_monitor: read J1939 traffic from SocketCAN or a recorded trace, decode it,
// run the fault state machine, and publish results to the console, a serial
// port, and UDP.
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "j1939/io.hpp"
#include "j1939/monitor.hpp"
#include "j1939/sinks.hpp"

using namespace j1939;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

class ConsoleSink : public Sink {
public:
    explicit ConsoleSink(bool verbose) : verbose_(verbose) {}
    void on_signal(const Signal& s, uint8_t src, uint64_t) override {
        if (verbose_) std::printf("  [SA %02X] SPN %-4u %-28s %10.2f %s\n", src, s.spn, s.name.c_str(), s.value, s.unit.c_str());
    }
    void on_dm1(const Dm1& d, uint8_t src, uint64_t) override {
        if (d.dtcs.empty()) return;
        std::printf("  [SA %02X] DM1: %zu active DTC(s):", src, d.dtcs.size());
        for (const auto& dtc : d.dtcs) std::printf(" SPN %u/FMI %u", dtc.spn, dtc.fmi);
        std::printf("\n");
    }
    void on_transition(const Transition& t) override {
        std::printf("STATE %-8s -> %-8s (%s)\n", to_string(t.from), to_string(t.to), t.reason.c_str());
        std::fflush(stdout);
    }

private:
    bool verbose_;
};

void usage() {
    std::puts(
        "usage: j1939_monitor (--iface IFACE | --replay FILE [--realtime])\n"
        "                     [--serial PATH] [--udp HOST:PORT] [--verbose]\n\n"
        "  --iface vcan0         read live frames from a SocketCAN interface\n"
        "  --replay trace.log    read a candump -L trace (as fast as possible)\n"
        "  --realtime            replay using the trace's original timing\n"
        "  --serial /dev/pts/3   write CSV log lines to a serial port (or file)\n"
        "  --udp 127.0.0.1:9000  send JSON events to the Qt dashboard\n"
        "  --verbose             print every decoded signal");
}

}  // namespace

int main(int argc, char** argv) {
    std::string iface, replay, serial_path, udp;
    bool realtime = false, verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--iface") iface = next();
        else if (a == "--replay") replay = next();
        else if (a == "--realtime") realtime = true;
        else if (a == "--serial") serial_path = next();
        else if (a == "--udp") udp = next();
        else if (a == "--verbose") verbose = true;
        else { usage(); return a == "--help" ? 0 : 2; }
    }
    if (iface.empty() == replay.empty()) { usage(); return 2; }

    try {
        std::unique_ptr<FrameSource> source;
        if (!iface.empty()) source = std::make_unique<SocketCanSource>(iface);
        else source = std::make_unique<LogFileSource>(replay, realtime);

        Monitor monitor;
        ConsoleSink console(verbose);
        monitor.add_sink(&console);

        std::unique_ptr<SerialLogger> serial;
        if (!serial_path.empty()) {
            serial = std::make_unique<SerialLogger>(serial_path);
            monitor.add_sink(serial.get());
            std::printf("serial log -> %s (%s)\n", serial_path.c_str(), serial->is_tty() ? "tty, 115200 8N1" : "file");
        }
        std::unique_ptr<UdpForwarder> forwarder;
        if (!udp.empty()) {
            const auto colon = udp.rfind(':');
            if (colon == std::string::npos) throw std::runtime_error("--udp expects HOST:PORT");
            forwarder = std::make_unique<UdpForwarder>(udp.substr(0, colon), static_cast<uint16_t>(std::stoi(udp.substr(colon + 1))));
            monitor.add_sink(forwarder.get());
            std::printf("udp -> %s\n", udp.c_str());
        }

        std::signal(SIGINT, on_sigint);
        std::signal(SIGTERM, on_sigint);
        std::printf("monitoring %s ... (Ctrl+C to stop)\n", iface.empty() ? replay.c_str() : iface.c_str());

        // Latency = time from the frame reaching user space to every sink having
        // been written. Stored per frame so we can report percentiles.
        std::vector<uint32_t> latency_us;
        latency_us.reserve(1 << 20);
        uint64_t first_us = 0, last_us = 0;

        while (!g_stop && !source->finished()) {
            auto frame = source->read(100);
            const uint64_t received = monotonic_us();
            if (!frame) {
                if (!iface.empty()) monitor.tick(received);  // stale-data detection on a quiet bus
                continue;
            }
            if (!iface.empty()) frame->timestamp_us = received;  // live: use monotonic clock
            monitor.process(*frame);
            const uint64_t done = monotonic_us();
            if (!first_us) first_us = received;
            last_us = done;
            latency_us.push_back(static_cast<uint32_t>(done - received));
        }

        const auto& st = monitor.stats();
        std::printf("\n--- summary ---------------------------------------------\n");
        std::printf("frames: %llu  decoded messages: %llu  multi-packet (BAM): %llu  ignored: %llu\n",
                    (unsigned long long)st.frames, (unsigned long long)st.decoded_messages,
                    (unsigned long long)st.bam_completed, (unsigned long long)st.ignored);
        std::printf("BAM sessions aborted: %llu   final state: %s\n",
                    (unsigned long long)monitor.transport().aborted_sessions(), to_string(monitor.state_machine().state()));
        if (serial) std::printf("serial lines dropped (port busy): %llu\n", (unsigned long long)serial->dropped_lines());
        if (!latency_us.empty() && last_us > first_us) {
            std::sort(latency_us.begin(), latency_us.end());
            auto pct = [&](double p) { return latency_us[static_cast<std::size_t>(p * (latency_us.size() - 1))]; };
            const double secs = (last_us - first_us) / 1e6;
            std::printf("throughput: %.0f frames/s over %.2f s\n", latency_us.size() / secs, secs);
            std::printf("processing latency (us): p50 %u  p99 %u  max %u\n", pct(0.50), pct(0.99), latency_us.back());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
