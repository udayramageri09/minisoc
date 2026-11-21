// Hardware abstraction layer the firmware is written against.
// On MiniSoC these calls become TLM transactions; on a real board they would be
// volatile pointer accesses and a WFI instruction. The driver code above this
// layer is identical in both cases - that is the point of a virtual prototype.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace hal {
void write32(uintptr_t addr, uint32_t value);
uint32_t read32(uintptr_t addr);
bool wait_for_irq(uint64_t timeout_ns);  // returns false on timeout
void delay_ns(uint64_t ns);
uint64_t now_ns();
void fail(const char* msg);
void log(const char* msg);
}  // namespace hal

namespace fw {
using Entry = std::function<int()>;
Entry find(const std::string& name);  // empty if unknown
}  // namespace fw
