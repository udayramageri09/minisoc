# UART: disabled writes ignored, FIFO full / overrun, drain timing, TX-empty IRQ.
source [file join [file dirname [info script]] lib.tcl]

# Disabled: byte must be dropped.
vp_write $::UART0_TXDATA 0x41
vp_wait 2000
expect_eq [string length [vp_uart_output]] 0 "nothing sent while disabled"

vp_write $::UART0_CTRL $::UART_CTRL_EN_MSK
expect_true [bits $::UART0_STATUS $::UART_STATUS_TX_EMPTY_MSK] "TX_EMPTY after enable"

# Fill the 8-deep FIFO faster than it drains (1 char/us), then one more -> OVERRUN.
foreach c [split "ABCDEFGH" ""] { vp_write $::UART0_TXDATA [scan $c %c] }
expect_true [bits $::UART0_STATUS $::UART_STATUS_TX_FULL_MSK] "TX_FULL with 8 queued"
vp_write $::UART0_TXDATA [scan "X" %c]
expect_true [bits $::UART0_STATUS $::UART_STATUS_OVERRUN_MSK] "OVERRUN on 9th byte"
vp_write $::UART0_STATUS $::UART_STATUS_OVERRUN_MSK
expect_true [expr {![bits $::UART0_STATUS $::UART_STATUS_OVERRUN_MSK]}] "OVERRUN cleared by W1C"

vp_wait 9000
expect_eq [vp_uart_output] "ABCDEFGH" "drained bytes (X was dropped)"

# TX-empty interrupt through the INTC.
vp_write $::INTC0_ENABLE [expr {1 << $::UART0_IRQ}]
vp_write $::UART0_CTRL [expr {$::UART_CTRL_EN_MSK | $::UART_CTRL_TXIE_MSK}]
expect_true [vp_wait_irq 100] "TX-empty IRQ asserted when FIFO is empty"
expect_eq [vp_read $::INTC0_ACTIVE] [expr {1 << $::UART0_IRQ}] "INTC ACTIVE shows UART line"
