// Firmware images that run on MiniSoC. Each returns 0 on success.
#include <cstdio>
#include <map>

#include "drivers.h"

namespace {

// Prints a banner over the UART. The regression checks the UART output.
int fw_hello() {
    drv::uart_init();
    drv::uart_puts("Hello from MiniSoC firmware\n");
    drv::uart_flush();
    return 0;
}

// Interrupt-driven periodic timer: 5 ticks of 10 us, each printed on the UART.
// Checks that the ISR count and the elapsed simulated time are correct.
int fw_timer_irq() {
    constexpr uint32_t kTicks = 1000;  // 1000 * 10 ns = 10 us
    constexpr int kWanted = 5;
    drv::uart_init();
    drv::intc_enable(1u << TIMER0_IRQ);
    const uint64_t t0 = hal::now_ns();
    drv::timer_start(kTicks, /*periodic=*/true, /*irq=*/true);

    int seen = 0;
    while (seen < kWanted) {
        if (!hal::wait_for_irq(50'000)) { hal::fail("timer interrupt did not arrive"); return 1; }
        if (!(drv::intc_active() & (1u << TIMER0_IRQ))) { hal::fail("IRQ raised but TIMER0 not active"); return 1; }
        drv::timer_ack();  // clear at source, otherwise the level IRQ stays high
        char line[32];
        std::snprintf(line, sizeof line, "tick %d\n", ++seen);
        drv::uart_puts(line);
    }
    drv::timer_stop();
    drv::uart_flush();

    const uint64_t elapsed = hal::now_ns() - t0;
    if (elapsed < kWanted * 10'000ull) { hal::fail("timer fired too early"); return 1; }
    char msg[64];
    std::snprintf(msg, sizeof msg, "5 ticks in %llu ns", static_cast<unsigned long long>(elapsed));
    hal::log(msg);
    return 0;
}

// Writes a pattern across SRAM and reads it back (catches decode/aliasing bugs).
int fw_mem_test() {
    for (uint32_t a = SRAM_BASE; a < SRAM_BASE + SRAM_SIZE; a += 256) hal::write32(a, a ^ 0xA5A5A5A5u);
    for (uint32_t a = SRAM_BASE; a < SRAM_BASE + SRAM_SIZE; a += 256)
        if (hal::read32(a) != (a ^ 0xA5A5A5A5u)) { hal::fail("SRAM readback mismatch"); return 1; }
    return 0;
}

}  // namespace

fw::Entry fw::find(const std::string& name) {
    static const std::map<std::string, Entry> images = {
        {"hello", fw_hello}, {"timer_irq", fw_timer_irq}, {"mem_test", fw_mem_test}};
    auto it = images.find(name);
    return it == images.end() ? Entry{} : it->second;
}
