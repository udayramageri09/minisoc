# Bus decode errors, register-level errors and fault injection.
source [file join [file dirname [info script]] lib.tcl]

expect_bus_error { vp_read 0x20000000 } "unmapped address"
expect_bus_error { vp_read [expr {$::TIMER0_BASE + 0x40}] } "hole inside TIMER0 window"
expect_bus_error { vp_read [expr {$::TIMER0_BASE + 0x02}] } "misaligned register access"

# Writes to read-only registers are ignored, not errors (like real hardware).
vp_write $::TIMER0_VALUE 0x1234
expect_eq [vp_read $::TIMER0_VALUE] 0 "RO VALUE unaffected by write"

# Fault injection: make a good address fail, check the error path, then clear.
vp_inject_error $::SRAM_BASE
expect_bus_error { vp_read $::SRAM_BASE } "injected fault on SRAM word 0"
vp_inject_error clear
vp_write $::SRAM_BASE 7
expect_eq [vp_read $::SRAM_BASE] 7 "SRAM word 0 works after clearing fault"
