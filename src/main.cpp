// MiniSoC top level: elaborates the platform from regmap.h and runs one test.
//
//   minisoc --script tests/test_x.tcl [--trace out.csv] [--timeout-us N]
//   minisoc --fw hello|timer_irq|mem_test [...]
//
// Exit codes (consumed by tools/regress.py):
//   0 passed, 1 test failed, 2 script/firmware error, 3 simulation timeout, 4 usage
#include <cstdlib>
#include <iostream>
#include <string>
#include <systemc>

#include "bus.h"
#include "cpu.h"
#include "memory.h"
#include "peripherals.h"
#include "regmap.h"

using namespace sc_core;

#ifndef MINISOC_REGMAP_TCL
#define MINISOC_REGMAP_TCL "generated/regmap.tcl"
#endif

// SystemC threads are coroutines (QuickThreads) that switch stacks by hand.
// ASan's fake-stack mode for stack-use-after-return cannot follow those switches
// and reports false SEGVs, so disable just that check in sanitizer builds.
#if defined(__SANITIZE_ADDRESS__)
extern "C" const char* __asan_default_options() { return "detect_stack_use_after_return=0"; }
#endif

static int usage() {
    std::cerr << "usage: minisoc (--script FILE.tcl | --fw NAME) [--trace FILE.csv] [--timeout-us N]\n";
    return 4;
}

int sc_main(int argc, char* argv[]) {
    std::string script, firmware, trace;
    double timeout_us = 1000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        if (a == "--script") script = argv[++i];
        else if (a == "--fw") firmware = argv[++i];
        else if (a == "--trace") trace = argv[++i];
        else if (a == "--timeout-us") timeout_us = std::atof(argv[++i]);
        else return usage();
    }
    if (script.empty() == firmware.empty()) return usage();
    sc_report_handler::set_actions(SC_WARNING, SC_DISPLAY);

    // ---- elaboration: instantiate and connect the platform ----
    Cpu cpu("cpu", script, firmware, MINISOC_REGMAP_TCL);
    Bus bus("bus", sc_time(MINISOC_BUS_LATENCY_NS, SC_NS));
    Memory sram("sram", SRAM_SIZE, sc_time(SRAM_LATENCY_NS, SC_NS));
    Uart uart("uart0", UART0_SIZE, sc_time(1, SC_US));
    Timer timer("timer0", TIMER0_SIZE, sc_time(10, SC_NS));
    Intc intc("intc0", INTC0_SIZE, MINISOC_NUM_IRQS);

    cpu.isock.bind(bus.tsock);
    bus.map("SRAM", SRAM_BASE, SRAM_SIZE);       bus.isock.bind(sram.tsock);
    bus.map("UART0", UART0_BASE, UART0_SIZE);    bus.isock.bind(uart.tsock);
    bus.map("TIMER0", TIMER0_BASE, TIMER0_SIZE); bus.isock.bind(timer.tsock);
    bus.map("INTC0", INTC0_BASE, INTC0_SIZE);    bus.isock.bind(intc.tsock);
    if (!trace.empty()) bus.enable_trace(trace);

    sc_signal<bool> timer_irq("timer_irq"), uart_irq("uart_irq"), cpu_irq("cpu_irq");
    timer.irq(timer_irq);
    uart.irq(uart_irq);
    intc.irq_in[TIMER0_IRQ](timer_irq);
    intc.irq_in[UART0_IRQ](uart_irq);
    intc.cpu_irq(cpu_irq);
    cpu.irq(cpu_irq);

    cpu.uart_output = [&] { return uart.output(); };
    cpu.inject_error = [&](uint64_t a) { bus.inject_error(a); };
    cpu.clear_errors = [&] { bus.clear_errors(); };

    // ---- simulation ----
    sc_start(sc_time(timeout_us, SC_US));

    std::cout << "STATS: sim_time=" << sc_time_stamp() << " transactions=" << cpu.transactions()
              << " timer_expirations=" << timer.expirations()
              << " ignored_accesses=" << (uart.ignored_accesses() + timer.ignored_accesses() + intc.ignored_accesses())
              << std::endl;
    switch (cpu.outcome()) {
        case Outcome::Passed: std::cout << "TEST PASSED" << std::endl; return 0;
        case Outcome::Failed: std::cout << "TEST FAILED (" << cpu.failures() << " checks)" << std::endl; return 1;
        case Outcome::Error: std::cout << "TEST ERROR: " << cpu.error() << std::endl; return 2;
        case Outcome::Running: std::cout << "TEST TIMEOUT after " << timeout_us << " us" << std::endl; return 3;
    }
    return 2;
}
