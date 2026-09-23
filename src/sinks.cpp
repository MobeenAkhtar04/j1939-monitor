#include "j1939/sinks.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace j1939 {
namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof buf, f, args);
    va_end(args);
    return buf;
}

}  // namespace

// ---------- SerialLogger ----------------------------------------------------------

SerialLogger::SerialLogger(const std::string& path) {
    // Open an existing device first; only create a file if nothing is there
    // (O_CREAT on a device node or symlink can be refused by the kernel).
    fd_ = ::open(path.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_APPEND);
    if (fd_ < 0 && errno == ENOENT)
        fd_ = ::open(path.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) throw std::runtime_error("cannot open serial output " + path + ": " + strerror(errno));

    termios tio{};
    if (::tcgetattr(fd_, &tio) == 0) {
        is_tty_ = true;
        ::cfmakeraw(&tio);                 // no echo, no line editing, 8 data bits
        ::cfsetispeed(&tio, B115200);
        ::cfsetospeed(&tio, B115200);
        tio.c_cflag &= ~(PARENB | CSTOPB);  // no parity, 1 stop bit
        tio.c_cflag |= CLOCAL | CREAD;
        ::tcsetattr(fd_, TCSANOW, &tio);
    }
}

SerialLogger::~SerialLogger() {
    if (fd_ >= 0) ::close(fd_);
}

void SerialLogger::write_line(const std::string& line) {
    const std::string out = line + "\r\n";
    const char* p = out.data();
    std::size_t left = out.size();
    while (left > 0) {
        const ssize_t n = ::write(fd_, p, left);
        if (n <= 0) {  // port buffer full (EAGAIN): drop the line, never block decoding
            ++dropped_;
            return;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
}

void SerialLogger::on_signal(const Signal& s, uint8_t src, uint64_t t_us) {
    write_line(fmt("SIG,%llu,%u,%u,%.3f,%s", (unsigned long long)(t_us / 1000), src, s.spn,
                   s.value, s.unit.c_str()));
}

void SerialLogger::on_dm1(const Dm1& dm1, uint8_t src, uint64_t t_us) {
    std::string line = fmt("DM1,%llu,%u,%u,%zu,", (unsigned long long)(t_us / 1000), src,
                           dm1.lamp_status, dm1.dtcs.size());
    for (std::size_t i = 0; i < dm1.dtcs.size(); ++i) {
        if (i) line += ';';
        line += fmt("%u:%u", dm1.dtcs[i].spn, dm1.dtcs[i].fmi);
    }
    write_line(line);
}

void SerialLogger::on_transition(const Transition& tr) {
    write_line(fmt("STATE,%llu,%s,%s,%s", (unsigned long long)(tr.at_us / 1000),
                   to_string(tr.from), to_string(tr.to), tr.reason.c_str()));
}

// ---------- UdpForwarder ----------------------------------------------------------

UdpForwarder::UdpForwarder(const std::string& host, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) throw std::runtime_error("socket(UDP) failed");
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) != 1) {
        ::close(fd_);
        throw std::runtime_error("invalid UDP host (use an IPv4 address): " + host);
    }
}

UdpForwarder::~UdpForwarder() {
    if (fd_ >= 0) ::close(fd_);
}

void UdpForwarder::send(const std::string& payload) {
    // Fire-and-forget: a missing dashboard must never stall the monitor.
    ::sendto(fd_, payload.data(), payload.size(), MSG_DONTWAIT,
             reinterpret_cast<const sockaddr*>(&dest_), sizeof dest_);
}

void UdpForwarder::on_signal(const Signal& s, uint8_t src, uint64_t) {
    send(fmt(R"({"type":"signal","spn":%u,"name":"%s","value":%.3f,"unit":"%s","src":%u})", s.spn,
             json_escape(s.name).c_str(), s.value, json_escape(s.unit).c_str(), src));
}

void UdpForwarder::on_dm1(const Dm1& dm1, uint8_t src, uint64_t) {
    std::string dtcs;
    for (std::size_t i = 0; i < dm1.dtcs.size(); ++i) {
        if (i) dtcs += ',';
        dtcs += fmt(R"({"spn":%u,"fmi":%u,"oc":%u})", dm1.dtcs[i].spn, dm1.dtcs[i].fmi,
                    dm1.dtcs[i].occurrences);
    }
    send(fmt(R"({"type":"dm1","lamp":%u,"dtcs":[%s],"src":%u})", dm1.lamp_status, dtcs.c_str(), src));
}

void UdpForwarder::on_transition(const Transition& tr) {
    send(fmt(R"({"type":"state","from":"%s","to":"%s","reason":"%s"})", to_string(tr.from),
             to_string(tr.to), json_escape(tr.reason).c_str()));
}

}  // namespace j1939
