#include "cpu.h"

#include <tcl.h>

#include <cstdio>
#include <iostream>

#include "../firmware/hal.h"

using namespace sc_core;

Cpu::Cpu(sc_module_name nm, std::string script, std::string firmware, std::string regmap_tcl)
    : sc_module(nm), isock("isock"), irq("irq"), script_(std::move(script)), firmware_(std::move(firmware)),
      regmap_tcl_(std::move(regmap_tcl)) {
    SC_THREAD(run);
    // SystemC threads run on small coroutine stacks (64 KiB default). The Tcl
    // interpreter and sanitizer builds need far more; overflowing it corrupts
    // memory silently in normal builds and crashes under ASan.
    set_stack_size(1 << 20);
}

void Cpu::transport(tlm::tlm_command cmd, uint64_t addr, uint32_t& data) {
    tlm::tlm_generic_payload gp;
    sc_time delay = SC_ZERO_TIME;
    gp.set_command(cmd);
    gp.set_address(addr);
    gp.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    gp.set_data_length(4);
    gp.set_streaming_width(4);
    gp.set_byte_enable_ptr(nullptr);
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    isock->b_transport(gp, delay);
    ++transactions_;
    wait(delay);  // synchronise after every access: simple and exact (no temporal decoupling)
    if (gp.is_response_error()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "bus error (%s) at 0x%08llx", gp.get_response_string().c_str(),
                      static_cast<unsigned long long>(addr));
        throw BusError(addr, buf);
    }
}

uint32_t Cpu::read32(uint64_t addr) {
    uint32_t d = 0;
    transport(tlm::TLM_READ_COMMAND, addr, d);
    return d;
}
void Cpu::write32(uint64_t addr, uint32_t value) { transport(tlm::TLM_WRITE_COMMAND, addr, value); }
void Cpu::wait_ns(double ns) { wait(sc_time(ns, SC_NS)); }

bool Cpu::wait_irq(double timeout_ns) {
    if (irq.read()) return true;
    wait(sc_time(timeout_ns, SC_NS), irq.posedge_event());
    return irq.read();
}

void Cpu::fail(const std::string& msg) {
    ++failures_;
    std::cout << "FAIL: " << msg << " @" << sc_time_stamp() << std::endl;
}
void Cpu::log(const std::string& msg) { std::cout << "LOG: " << msg << " @" << sc_time_stamp() << std::endl; }

void Cpu::run() {
    try {
        if (!firmware_.empty())
            run_firmware();
        else
            run_tcl();
        if (outcome_ == Outcome::Running) outcome_ = failures_ ? Outcome::Failed : Outcome::Passed;
    } catch (const std::exception& e) {
        error_ = e.what();
        outcome_ = Outcome::Error;
    }
    sc_stop();
}

// ------------------------------------------------------------ firmware ----
namespace {
Cpu* g_cpu = nullptr;
}
namespace hal {
void write32(uintptr_t a, uint32_t v) { g_cpu->write32(a, v); }
uint32_t read32(uintptr_t a) { return g_cpu->read32(a); }
bool wait_for_irq(uint64_t timeout_ns) { return g_cpu->wait_irq(static_cast<double>(timeout_ns)); }
void delay_ns(uint64_t ns) { g_cpu->wait_ns(static_cast<double>(ns)); }
uint64_t now_ns() { return static_cast<uint64_t>(sc_time_stamp().to_seconds() * 1e9 + 0.5); }
void fail(const char* m) { g_cpu->fail(m); }
void log(const char* m) { g_cpu->log(m); }
}  // namespace hal

void Cpu::run_firmware() {
    g_cpu = this;
    auto fn = fw::find(firmware_);
    if (!fn) throw std::runtime_error("unknown firmware '" + firmware_ + "'");
    const int rc = fn();
    if (rc != 0) fail("firmware returned " + std::to_string(rc));
}

// ------------------------------------------------------------------ Tcl ----
namespace {
Cpu* self(ClientData cd) { return static_cast<Cpu*>(cd); }

bool get_u64(Tcl_Interp* ip, Tcl_Obj* o, uint64_t& out) {
    Tcl_WideInt w;
    if (Tcl_GetWideIntFromObj(ip, o, &w) != TCL_OK) return false;
    out = static_cast<uint64_t>(w);
    return true;
}

// Wrap each command so C++ exceptions (bus errors) become Tcl errors that a
// test can `catch`, instead of unwinding through the C interpreter.
template <typename F>
int guarded(Tcl_Interp* ip, F&& f) {
    try {
        return f();
    } catch (const std::exception& e) {
        Tcl_SetObjResult(ip, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    }
}

int cmd_read(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "addr"); return TCL_ERROR; }
    return guarded(ip, [&] {
        uint64_t a;
        if (!get_u64(ip, objv[1], a)) return TCL_ERROR;
        Tcl_SetObjResult(ip, Tcl_NewWideIntObj(self(cd)->read32(a)));
        return TCL_OK;
    });
}
int cmd_write(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 3) { Tcl_WrongNumArgs(ip, 1, objv, "addr value"); return TCL_ERROR; }
    return guarded(ip, [&] {
        uint64_t a, v;
        if (!get_u64(ip, objv[1], a) || !get_u64(ip, objv[2], v)) return TCL_ERROR;
        self(cd)->write32(a, static_cast<uint32_t>(v));
        return TCL_OK;
    });
}
int cmd_wait(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "ns"); return TCL_ERROR; }
    double ns;
    if (Tcl_GetDoubleFromObj(ip, objv[1], &ns) != TCL_OK) return TCL_ERROR;
    self(cd)->wait_ns(ns);
    return TCL_OK;
}
int cmd_wait_irq(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "timeout_ns"); return TCL_ERROR; }
    double ns;
    if (Tcl_GetDoubleFromObj(ip, objv[1], &ns) != TCL_OK) return TCL_ERROR;
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(self(cd)->wait_irq(ns)));
    return TCL_OK;
}
int cmd_time(ClientData, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    Tcl_SetObjResult(ip, Tcl_NewDoubleObj(sc_time_stamp().to_seconds() * 1e9));
    return TCL_OK;
}
int cmd_fail(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "message"); return TCL_ERROR; }
    self(cd)->fail(Tcl_GetString(objv[1]));
    return TCL_OK;
}
int cmd_log(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "message"); return TCL_ERROR; }
    self(cd)->log(Tcl_GetString(objv[1]));
    return TCL_OK;
}
int cmd_uart(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    Tcl_SetObjResult(ip, Tcl_NewStringObj(self(cd)->uart_output().c_str(), -1));
    return TCL_OK;
}
int cmd_inject(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    if (objc == 2 && std::string(Tcl_GetString(objv[1])) == "clear") { self(cd)->clear_errors(); return TCL_OK; }
    uint64_t a;
    if (objc != 2 || !get_u64(ip, objv[1], a)) { Tcl_WrongNumArgs(ip, 1, objv, "addr|clear"); return TCL_ERROR; }
    self(cd)->inject_error(a);
    return TCL_OK;
}
}  // namespace

void Cpu::run_tcl() {
    Tcl_Interp* ip = Tcl_CreateInterp();
    struct Cmd { const char* name; Tcl_ObjCmdProc* fn; };
    const Cmd cmds[] = {{"vp_read", cmd_read},   {"vp_write", cmd_write},  {"vp_wait", cmd_wait},
                        {"vp_wait_irq", cmd_wait_irq}, {"vp_time", cmd_time}, {"vp_fail", cmd_fail},
                        {"vp_log", cmd_log},     {"vp_uart_output", cmd_uart}, {"vp_inject_error", cmd_inject}};
    for (const auto& c : cmds) Tcl_CreateObjCommand(ip, c.name, c.fn, this, nullptr);

    for (const std::string& file : {regmap_tcl_, script_}) {
        if (Tcl_EvalFile(ip, file.c_str()) != TCL_OK) {
            const char* info = Tcl_GetVar(ip, "errorInfo", TCL_GLOBAL_ONLY);
            std::string msg = info ? info : Tcl_GetStringResult(ip);
            Tcl_DeleteInterp(ip);
            throw std::runtime_error("Tcl error in " + file + ": " + msg);
        }
    }
    Tcl_DeleteInterp(ip);
}
