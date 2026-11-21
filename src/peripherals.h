// Concrete peripherals: Timer, Uart, Intc.
// Register offsets and bit masks come from the generated regmap.h, so the
// models, the firmware and the Tcl tests can never disagree on the map.
//
// Modelling notes (loosely-timed style):
//  * The timer is event-driven: it schedules one event at the expiry time
//    instead of ticking every 10 ns, and computes VALUE from sc_time_stamp().
//  * Signals have exactly one writer process. Register writes arrive in the
//    CPU's thread, so they only notify an event; a dedicated SC_METHOD in the
//    peripheral drives the IRQ output (avoids multiple-driver errors).
#pragma once

#include <deque>
#include <iostream>
#include <string>
#include <systemc>

#include "reg_peripheral.h"
#include "regmap.h"

// ---------------------------------------------------------------- Timer ----
class Timer : public RegPeripheral {
public:
    sc_core::sc_out<bool> irq;
    SC_HAS_PROCESS(Timer);

    Timer(sc_core::sc_module_name nm, uint64_t size, sc_core::sc_time tick)
        : RegPeripheral(nm, size, sc_core::sc_time(1, sc_core::SC_NS)), irq("irq"), tick_(tick) {
        SC_METHOD(on_expire);
        sensitive << expire_ev_;
        dont_initialize();
        SC_METHOD(drive_irq);
        sensitive << irq_ev_;
    }
    unsigned expirations() const { return expirations_; }

protected:
    bool reg_read(uint32_t off, uint32_t& v) override {
        switch (off) {
            case TIMER_CTRL_OFF: v = ctrl_; return true;
            case TIMER_LOAD_OFF: v = load_; return true;
            case TIMER_VALUE_OFF: v = remaining(); return true;
            case TIMER_STATUS_OFF: v = status_; return true;
            default: return false;
        }
    }
    bool reg_write(uint32_t off, uint32_t v) override {
        switch (off) {
            case TIMER_CTRL_OFF: {
                const bool was_en = ctrl_ & TIMER_CTRL_EN_MSK;
                ctrl_ = v & (TIMER_CTRL_EN_MSK | TIMER_CTRL_PERIODIC_MSK | TIMER_CTRL_IE_MSK);
                const bool en = ctrl_ & TIMER_CTRL_EN_MSK;
                if (en && !was_en) start();
                if (!en) expire_ev_.cancel();
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return true;
            }
            case TIMER_LOAD_OFF:
                load_ = v;
                if (ctrl_ & TIMER_CTRL_EN_MSK) start();
                return true;
            case TIMER_VALUE_OFF: note_ignored("write to RO VALUE", off); return true;
            case TIMER_STATUS_OFF:  // write-1-to-clear
                status_ &= ~v;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return true;
            default: return false;
        }
    }

private:
    void start() {
        expire_ev_.cancel();
        start_ = sc_core::sc_time_stamp();
        if (load_ == 0) return;  // LOAD=0 means idle
        expire_ev_.notify(tick_ * static_cast<double>(load_));
    }
    uint32_t remaining() const {
        if (!(ctrl_ & TIMER_CTRL_EN_MSK) || load_ == 0) return 0;
        const double elapsed = (sc_core::sc_time_stamp() - start_) / tick_;
        return elapsed >= load_ ? 0 : load_ - static_cast<uint32_t>(elapsed);
    }
    void on_expire() {
        status_ |= TIMER_STATUS_EXPIRED_MSK;
        ++expirations_;
        if (ctrl_ & TIMER_CTRL_PERIODIC_MSK)
            start();
        else
            ctrl_ &= ~TIMER_CTRL_EN_MSK;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void drive_irq() { irq.write((status_ & TIMER_STATUS_EXPIRED_MSK) && (ctrl_ & TIMER_CTRL_IE_MSK)); }

    sc_core::sc_time tick_, start_;
    sc_core::sc_event expire_ev_, irq_ev_;
    uint32_t ctrl_ = 0, load_ = 0, status_ = 0;
    unsigned expirations_ = 0;
};

// ----------------------------------------------------------------- UART ----
class Uart : public RegPeripheral {
public:
    static constexpr unsigned kFifoDepth = 8;
    sc_core::sc_out<bool> irq;
    SC_HAS_PROCESS(Uart);

    Uart(sc_core::sc_module_name nm, uint64_t size, sc_core::sc_time char_time)
        : RegPeripheral(nm, size, sc_core::sc_time(1, sc_core::SC_NS)), irq("irq"), char_time_(char_time) {
        SC_THREAD(drain);
        SC_METHOD(drive_irq);
        sensitive << irq_ev_;
    }
    const std::string& output() const { return output_; }

protected:
    bool reg_read(uint32_t off, uint32_t& v) override {
        switch (off) {
            case UART_TXDATA_OFF: note_ignored("read of WO TXDATA", off); v = 0; return true;
            case UART_STATUS_OFF:
                v = (fifo_.empty() ? UART_STATUS_TX_EMPTY_MSK : 0) |
                    (fifo_.size() >= kFifoDepth ? UART_STATUS_TX_FULL_MSK : 0) | (overrun_ ? UART_STATUS_OVERRUN_MSK : 0);
                return true;
            case UART_CTRL_OFF: v = ctrl_; return true;
            default: return false;
        }
    }
    bool reg_write(uint32_t off, uint32_t v) override {
        switch (off) {
            case UART_TXDATA_OFF:
                if (!(ctrl_ & UART_CTRL_EN_MSK)) { note_ignored("TXDATA write while disabled", off); return true; }
                if (fifo_.size() >= kFifoDepth) { overrun_ = true; return true; }
                fifo_.push_back(static_cast<char>(v & UART_TXDATA_DATA_MSK));
                data_ev_.notify(sc_core::SC_ZERO_TIME);
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return true;
            case UART_STATUS_OFF:  // only OVERRUN is clearable (W1C)
                if (v & UART_STATUS_OVERRUN_MSK) overrun_ = false;
                return true;
            case UART_CTRL_OFF:
                ctrl_ = v & (UART_CTRL_EN_MSK | UART_CTRL_TXIE_MSK);
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return true;
            default: return false;
        }
    }

private:
    void drain() {
        for (;;) {
            while (fifo_.empty()) wait(data_ev_);
            wait(char_time_);  // time to shift one character out
            const char c = fifo_.front();
            fifo_.pop_front();
            output_ += c;
            if (c == '\n') {
                std::cout << "[" << name() << " @" << sc_core::sc_time_stamp() << "] " << line_ << std::endl;
                line_.clear();
            } else {
                line_ += c;
            }
            irq_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    // Level "TX empty" interrupt, like a 16550 THRE interrupt.
    void drive_irq() { irq.write((ctrl_ & UART_CTRL_EN_MSK) && (ctrl_ & UART_CTRL_TXIE_MSK) && fifo_.empty()); }

    sc_core::sc_time char_time_;
    sc_core::sc_event data_ev_, irq_ev_;
    std::deque<char> fifo_;
    std::string output_, line_;
    uint32_t ctrl_ = 0;
    bool overrun_ = false;
};

// ----------------------------------------------------------------- INTC ----
class Intc : public RegPeripheral {
public:
    sc_core::sc_vector<sc_core::sc_in<bool>> irq_in;
    sc_core::sc_out<bool> cpu_irq;
    SC_HAS_PROCESS(Intc);

    Intc(sc_core::sc_module_name nm, uint64_t size, unsigned lines)
        : RegPeripheral(nm, size, sc_core::sc_time(1, sc_core::SC_NS)), irq_in("irq_in", lines), cpu_irq("cpu_irq") {
        SC_METHOD(update);
        for (auto& in : irq_in) sensitive << in;
        sensitive << enable_ev_;
    }

protected:
    bool reg_read(uint32_t off, uint32_t& v) override {
        switch (off) {
            case INTC_PENDING_OFF: v = pending(); return true;
            case INTC_ENABLE_OFF: v = enable_; return true;
            case INTC_ACTIVE_OFF: v = pending() & enable_; return true;
            default: return false;
        }
    }
    bool reg_write(uint32_t off, uint32_t v) override {
        switch (off) {
            case INTC_ENABLE_OFF:
                enable_ = v & ((1u << irq_in.size()) - 1);
                enable_ev_.notify(sc_core::SC_ZERO_TIME);
                return true;
            case INTC_PENDING_OFF:
            case INTC_ACTIVE_OFF: note_ignored("write to RO register", off); return true;
            default: return false;
        }
    }

private:
    uint32_t pending() const {
        uint32_t p = 0;
        for (unsigned i = 0; i < irq_in.size(); ++i)
            if (irq_in[i].read()) p |= 1u << i;
        return p;
    }
    void update() { cpu_irq.write((pending() & enable_) != 0); }

    sc_core::sc_event enable_ev_;
    uint32_t enable_ = 0;
};
