// "CPU" initiator. It does not execute an instruction set; it runs either
//  (a) a Tcl test script (vp_read / vp_write / vp_wait_irq ... commands), or
//  (b) a host-compiled firmware routine written against firmware/hal.h.
// Every access becomes a TLM-2.0 blocking transaction through the bus, so the
// peripherals see exactly the register traffic real driver code would produce.
#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

struct BusError : std::runtime_error {
    uint64_t addr;
    BusError(uint64_t a, const std::string& what) : std::runtime_error(what), addr(a) {}
};

enum class Outcome { Running, Passed, Failed, Error };

class Cpu : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<Cpu> isock;
    sc_core::sc_in<bool> irq;
    SC_HAS_PROCESS(Cpu);

    Cpu(sc_core::sc_module_name nm, std::string script, std::string firmware, std::string regmap_tcl);

    // HAL used by both Tcl commands and firmware
    uint32_t read32(uint64_t addr);
    void write32(uint64_t addr, uint32_t value);
    void wait_ns(double ns);
    bool wait_irq(double timeout_ns);
    void fail(const std::string& msg);
    void log(const std::string& msg);

    Outcome outcome() const { return outcome_; }
    unsigned failures() const { return failures_; }
    const std::string& error() const { return error_; }
    unsigned transactions() const { return transactions_; }

    // hooks the Tcl layer needs into the rest of the platform
    std::function<std::string()> uart_output;
    std::function<void(uint64_t)> inject_error;
    std::function<void()> clear_errors;

private:
    void run();
    void run_tcl();
    void run_firmware();
    void transport(tlm::tlm_command cmd, uint64_t addr, uint32_t& data);

    std::string script_, firmware_, regmap_tcl_;
    Outcome outcome_ = Outcome::Running;
    unsigned failures_ = 0, transactions_ = 0;
    std::string error_;
};
