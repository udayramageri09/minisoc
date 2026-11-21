# SRAM read/write, boundaries, and a seeded random pattern.
source [file join [file dirname [info script]] lib.tcl]

vp_write $::SRAM_BASE 0xDEADBEEF
expect_eq [vp_read $::SRAM_BASE] 0xDEADBEEF "first word"

set last [expr {$::SRAM_BASE + $::SRAM_SIZE - 4}]
vp_write $last 0x12345678
expect_eq [vp_read $last] 0x12345678 "last word"

expect_bus_error { vp_read [expr {$::SRAM_BASE + $::SRAM_SIZE}] } "one past the end of SRAM"

# Seeded random pattern: reproducible, so any failure can be replayed exactly.
expr {srand(2026)}
set addrs {}
array set model {}
for {set i 0} {$i < 200} {incr i} {
    set a [expr {$::SRAM_BASE + 4 * int(rand() * ($::SRAM_SIZE / 4))}]
    set v [expr {int(rand() * 0xFFFFFFFF)}]
    vp_write $a $v
    set model($a) $v
}
set bad 0
foreach a [array names model] {
    if {[vp_read $a] != $model($a)} { incr bad }
}
expect_eq $bad 0 "random readback mismatches"
