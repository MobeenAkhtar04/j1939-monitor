#pragma once
#include <netinet/in.h>

#include <cstdint>
#include <string>

#include "j1939/decoder.hpp"
#include "j1939/fault_state_machine.hpp"

namespace j1939 {

// Anything that wants decoded output implements this interface. The Monitor
// fans every event out to all registered sinks.
class Sink {
public:
    virtual ~Sink() = default;
    virtual void on_signal(const Signal& s, uint8_t source, uint64_t t_us) = 0;
    virtual void on_dm1(const Dm1& dm1, uint8_t source, uint64_t t_us) = 0;
    virtual void on_transition(const Transition& tr) = 0;
};

// Writes one CSV line per event to a serial device (configured 115200 8N1 raw).
// If the path is not a TTY (e.g. a regular file) it is simply appended to,
// which is handy for tests and for capturing logs without hardware.
//
//   SIG,<t_ms>,<src>,<spn>,<value>,<unit>
//   DM1,<t_ms>,<src>,<lamp>,<count>,<spn>:<fmi>;<spn>:<fmi>...
//   STATE,<t_ms>,<from>,<to>,<reason>
class SerialLogger : public Sink {
public:
    explicit SerialLogger(const std::string& path);
    ~SerialLogger() override;
    SerialLogger(const SerialLogger&) = delete;
    SerialLogger& operator=(const SerialLogger&) = delete;

    void on_signal(const Signal& s, uint8_t source, uint64_t t_us) override;
    void on_dm1(const Dm1& dm1, uint8_t source, uint64_t t_us) override;
    void on_transition(const Transition& tr) override;
    bool is_tty() const { return is_tty_; }
    uint64_t dropped_lines() const { return dropped_; }

private:
    void write_line(const std::string& line);
    int fd_ = -1;
    bool is_tty_ = false;
    uint64_t dropped_ = 0;
};

// Sends one small JSON datagram per event over UDP (consumed by the Qt dashboard):
//   {"type":"signal","spn":190,"name":"Engine Speed","value":1500.0,"unit":"rpm","src":0}
//   {"type":"dm1","lamp":4,"dtcs":[{"spn":110,"fmi":0,"oc":1}],"src":0}
//   {"type":"state","from":"Normal","to":"Warning","reason":"..."}
class UdpForwarder : public Sink {
public:
    UdpForwarder(const std::string& host, uint16_t port);
    ~UdpForwarder() override;
    UdpForwarder(const UdpForwarder&) = delete;
    UdpForwarder& operator=(const UdpForwarder&) = delete;

    void on_signal(const Signal& s, uint8_t source, uint64_t t_us) override;
    void on_dm1(const Dm1& dm1, uint8_t source, uint64_t t_us) override;
    void on_transition(const Transition& tr) override;

private:
    void send(const std::string& payload);
    int fd_ = -1;
    sockaddr_in dest_{};
};

}  // namespace j1939
