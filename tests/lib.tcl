# Shared helpers for MiniSoC Tcl tests. Source with:
#   source [file join [file dirname [info script]] lib.tcl]

# Hex-format integers; leave strings (e.g. UART output) as they are.
proc hex {v} {
    if {[string is wideinteger -strict $v]} { return [format "0x%08X (%d)" $v $v] }
    return \"$v\"
}

proc expect_eq {actual expected what} {
    if {$actual ne $expected && !([string is wideinteger -strict $actual] && [string is wideinteger -strict $expected] && $actual == $expected)} {
        vp_fail "$what: expected [hex $expected], got [hex $actual]"
    } else {
        vp_log "ok: $what = [hex $actual]"
    }
}

proc expect_true {cond what} {
    if {!$cond} { vp_fail $what } else { vp_log "ok: $what" }
}

# Run a body that must raise a bus error; fail if it does not.
proc expect_bus_error {body what} {
    if {[catch {uplevel 1 $body} err]} {
        if {[string match "*bus error*" $err]} { vp_log "ok: $what ($err)"; return }
        vp_fail "$what: unexpected error '$err'"
    } else {
        vp_fail "$what: access succeeded, bus error expected"
    }
}

proc bits {reg mask} { return [expr {([vp_read $reg] & $mask) != 0}] }
