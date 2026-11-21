# Timer: one-shot expiry time, VALUE countdown, W1C status, periodic mode, masking.
source [file join [file dirname [info script]] lib.tcl]

set EN $::TIMER_CTRL_EN_MSK
set PER $::TIMER_CTRL_PERIODIC_MSK
set IE $::TIMER_CTRL_IE_MSK

# One-shot, polling: 500 ticks * 10 ns = 5 us.
vp_write $::TIMER0_LOAD 500
set t0 [vp_time]
vp_write $::TIMER0_CTRL $EN
vp_wait 2000
set v [vp_read $::TIMER0_VALUE]
expect_true [expr {$v > 250 && $v < 320}] "VALUE counting down ($v ticks left after ~2us)"
while {![bits $::TIMER0_STATUS $::TIMER_STATUS_EXPIRED_MSK]} { vp_wait 100 }
set dt [expr {[vp_time] - $t0}]
expect_true [expr {$dt >= 5000 && $dt <= 5200}] "one-shot expired after $dt ns (expected ~5000)"
expect_eq [expr {[vp_read $::TIMER0_CTRL] & $EN}] 0 "one-shot clears EN"
vp_write $::TIMER0_STATUS $::TIMER_STATUS_EXPIRED_MSK
expect_eq [vp_read $::TIMER0_STATUS] 0 "EXPIRED cleared by W1C"

# Exact timing via interrupt (no polling slack): 300 ticks must take exactly 3000 ns
# from the CTRL write. Catches off-by-one-tick bugs that a polling loop would hide.
vp_write $::INTC0_ENABLE [expr {1 << $::TIMER0_IRQ}]
vp_write $::TIMER0_LOAD 300
set t0 [vp_time]
vp_write $::TIMER0_CTRL [expr {$EN | $IE}]
expect_true [vp_wait_irq 10000] "one-shot IRQ arrived"
expect_eq [expr {round([vp_time] - $t0)}] 3000 "IRQ latency from CTRL write (ns)"
vp_write $::TIMER0_STATUS $::TIMER_STATUS_EXPIRED_MSK

# IRQ masked at the INTC: line pending but CPU IRQ stays low.
vp_write $::INTC0_ENABLE 0
vp_write $::TIMER0_LOAD 100
vp_write $::TIMER0_CTRL [expr {$EN | $IE}]
expect_true [expr {![vp_wait_irq 3000]}] "no CPU IRQ while INTC line disabled"
expect_eq [vp_read $::INTC0_PENDING] [expr {1 << $::TIMER0_IRQ}] "INTC PENDING has timer line"
vp_write $::TIMER0_STATUS $::TIMER_STATUS_EXPIRED_MSK

# Periodic with interrupts: count 4 expirations.
vp_write $::INTC0_ENABLE [expr {1 << $::TIMER0_IRQ}]
vp_write $::TIMER0_CTRL 0
vp_write $::TIMER0_LOAD 200
vp_write $::TIMER0_CTRL [expr {$EN | $PER | $IE}]
set n 0
for {set i 0} {$i < 4} {incr i} {
    if {[vp_wait_irq 10000]} { incr n; vp_write $::TIMER0_STATUS $::TIMER_STATUS_EXPIRED_MSK }
}
vp_write $::TIMER0_CTRL 0
expect_eq $n 4 "periodic interrupts received"
