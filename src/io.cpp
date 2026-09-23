#include "j1939/io.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace j1939 {

uint64_t monotonic_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------- candump log format -------------------------------------------------

std::optional<CanFrame> parse_candump_line(const std::string& line) {
    // "(sec.usec) iface ID#DATA"
    unsigned long long sec = 0, usec = 0;
    char iface[32];
    char body[64];
    if (std::sscanf(line.c_str(), " (%llu.%llu) %31s %63s", &sec, &usec, iface, body) != 4)
        return std::nullopt;

    const char* hash = std::strchr(body, '#');
    if (!hash) return std::nullopt;
    const std::size_t id_len = static_cast<std::size_t>(hash - body);
    if (id_len != 8) return std::nullopt;  // J1939 needs 29-bit (8 hex digit) IDs

    CanFrame f;
    f.id = static_cast<uint32_t>(std::strtoul(std::string(body, id_len).c_str(), nullptr, 16)) &
           CAN_EFF_MASK;
    f.timestamp_us = sec * 1'000'000ULL + usec;

    const char* hex = hash + 1;
    const std::size_t hex_len = std::strlen(hex);
    if (hex_len % 2 != 0 || hex_len > 16) return std::nullopt;
    f.dlc = static_cast<uint8_t>(hex_len / 2);
    for (std::size_t i = 0; i < f.dlc; ++i) {
        unsigned byte = 0;
        if (std::sscanf(hex + 2 * i, "%2x", &byte) != 1) return std::nullopt;
        f.data[i] = static_cast<uint8_t>(byte);
    }
    return f;
}

std::string format_candump_line(const CanFrame& f, const std::string& iface) {
    char buf[96];
    int n = std::snprintf(buf, sizeof buf, "(%" PRIu64 ".%06" PRIu64 ") %s %08X#",
                          f.timestamp_us / 1'000'000, f.timestamp_us % 1'000'000,
                          iface.c_str(), f.id);
    for (std::size_t i = 0; i < f.dlc; ++i)
        n += std::snprintf(buf + n, sizeof buf - n, "%02X", f.data[i]);
    return buf;
}

// ---------- SocketCAN ---------------------------------------------------------------

namespace {
int open_can_socket(const std::string& iface) {
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) throw std::runtime_error("socket(PF_CAN) failed: " + std::string(strerror(errno)));

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        ::close(fd);
        throw std::runtime_error("CAN interface '" + iface + "' not found. Run scripts/setup_vcan.sh");
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(fd);
        throw std::runtime_error("bind(" + iface + ") failed: " + std::string(strerror(errno)));
    }
    return fd;
}
}  // namespace

SocketCanSource::SocketCanSource(const std::string& iface) : fd_(open_can_socket(iface)) {
    // Kernel-side filter: only extended (29-bit) data frames reach user space.
    can_filter filter{};
    filter.can_id = CAN_EFF_FLAG;
    filter.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG;
    ::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof filter);
}

SocketCanSource::~SocketCanSource() {
    if (fd_ >= 0) ::close(fd_);
}

std::optional<CanFrame> SocketCanSource::read(int timeout_ms) {
    pollfd pfd{fd_, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return std::nullopt;

    can_frame raw{};
    if (::read(fd_, &raw, sizeof raw) != static_cast<ssize_t>(sizeof raw)) return std::nullopt;

    CanFrame f;
    f.timestamp_us = monotonic_us();
    f.id = raw.can_id & CAN_EFF_MASK;
    f.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
    std::memcpy(f.data.data(), raw.data, f.dlc);
    return f;
}

SocketCanWriter::SocketCanWriter(const std::string& iface) : fd_(open_can_socket(iface)) {}

SocketCanWriter::~SocketCanWriter() {
    if (fd_ >= 0) ::close(fd_);
}

bool SocketCanWriter::write(const CanFrame& f) {
    can_frame raw{};
    raw.can_id = (f.id & CAN_EFF_MASK) | CAN_EFF_FLAG;
    raw.can_dlc = f.dlc;
    std::memcpy(raw.data, f.data.data(), f.dlc);
    return ::write(fd_, &raw, sizeof raw) == static_cast<ssize_t>(sizeof raw);
}

// ---------- log replay ----------------------------------------------------------------

LogFileSource::LogFileSource(const std::string& path, bool realtime)
    : in_(path), realtime_(realtime) {
    if (!in_) throw std::runtime_error("cannot open trace file: " + path);
}

std::optional<CanFrame> LogFileSource::read(int /*timeout_ms*/) {
    std::string line;
    while (std::getline(in_, line)) {
        auto f = parse_candump_line(line);
        if (!f) continue;  // skip comments / malformed lines
        if (realtime_) {
            if (!first_log_us_) {
                first_log_us_ = f->timestamp_us;
                start_wall_us_ = monotonic_us();
            }
            const uint64_t due = start_wall_us_ + (f->timestamp_us - *first_log_us_);
            const uint64_t now = monotonic_us();
            if (due > now) std::this_thread::sleep_for(std::chrono::microseconds(due - now));
            f->timestamp_us = monotonic_us();
        }
        return f;
    }
    done_ = true;
    return std::nullopt;
}

}  // namespace j1939
