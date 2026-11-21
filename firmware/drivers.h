// Minimal bare-metal style drivers. Only regmap.h + hal.h are used.
#pragma once

#include <cstdint>

#include "hal.h"
#include "regmap.h"

namespace drv {

inline void uart_init(bool tx_irq = false) {
    hal::write32(UART0_CTRL, UART_CTRL_EN_MSK | (tx_irq ? UART_CTRL_TXIE_MSK : 0));
}
inline void uart_putc(char c) {
    while (hal::read32(UART0_STATUS) & UART_STATUS_TX_FULL_MSK) hal::delay_ns(200);  // back-pressure
    hal::write32(UART0_TXDATA, static_cast<uint8_t>(c));
}
inline void uart_puts(const char* s) {
    while (*s) uart_putc(*s++);
}
inline void uart_flush() {
    while (!(hal::read32(UART0_STATUS) & UART_STATUS_TX_EMPTY_MSK)) hal::delay_ns(200);
}

inline void timer_start(uint32_t ticks, bool periodic, bool irq) {
    hal::write32(TIMER0_CTRL, 0);
    hal::write32(TIMER0_STATUS, TIMER_STATUS_EXPIRED_MSK);
    hal::write32(TIMER0_LOAD, ticks);
    hal::write32(TIMER0_CTRL, TIMER_CTRL_EN_MSK | (periodic ? TIMER_CTRL_PERIODIC_MSK : 0) | (irq ? TIMER_CTRL_IE_MSK : 0));
}
inline void timer_stop() { hal::write32(TIMER0_CTRL, 0); }
inline void timer_ack() { hal::write32(TIMER0_STATUS, TIMER_STATUS_EXPIRED_MSK); }

inline void intc_enable(uint32_t mask) { hal::write32(INTC0_ENABLE, mask); }
inline uint32_t intc_active() { return hal::read32(INTC0_ACTIVE); }

}  // namespace drv
