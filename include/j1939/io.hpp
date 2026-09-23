#pragma once
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

#include "j1939/frame.hpp"

namespace j1939 {

uint64_t monotonic_us();

// ---------- candump log format -------------------------------------------------
// Same format as `candump -L`, so traces work with can-utils (canplayer, etc.):
//   (1700000000.123456) vcan0 18FEEE00#6E0078FFFFFFFFFF
std::optional<CanFrame> parse_candump_line(const std::string& line);
std::string format_candump_line(const CanFrame& f, const std::string& iface = "vcan0");

// ---------- frame sources --------------------------------------------------------
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // Wait up to timeout_ms for a frame. std::nullopt on timeout or end of input.
    virtual std::optional<CanFrame> read(int timeout_ms) = 0;
    virtual bool finished() const { return false; }
};

// Live bus via Linux SocketCAN (works with real adapters and virtual vcan devices).
class SocketCanSource : public FrameSource {
public:
    explicit SocketCanSource(const std::string& iface);
    ~SocketCanSource() override;
    SocketCanSource(const SocketCanSource&) = delete;
    SocketCanSource& operator=(const SocketCanSource&) = delete;
    std::optional<CanFrame> read(int timeout_ms) override;

private:
    int fd_ = -1;
};

// Recorded trace. With realtime=true, frames are released on their original
// timing; otherwise they are replayed as fast as possible (for benchmarks/tests).
class LogFileSource : public FrameSource {
public:
    LogFileSource(const std::string& path, bool realtime);
    std::optional<CanFrame> read(int timeout_ms) override;
    bool finished() const override { return done_; }

private:
    std::ifstream in_;
    bool realtime_;
    bool done_ = false;
    std::optional<uint64_t> first_log_us_;
    uint64_t start_wall_us_ = 0;
};

// ---------- frame sinks (used by the simulator) -----------------------------------
class SocketCanWriter {
public:
    explicit SocketCanWriter(const std::string& iface);
    ~SocketCanWriter();
    SocketCanWriter(const SocketCanWriter&) = delete;
    SocketCanWriter& operator=(const SocketCanWriter&) = delete;
    bool write(const CanFrame& f);

private:
    int fd_ = -1;
};

}  // namespace j1939
