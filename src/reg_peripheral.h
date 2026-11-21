// Base class for every memory-mapped register peripheral.
// It owns the TLM target socket and does the protocol checks common to all
// register blocks (32-bit aligned single-word access, no byte enables), then
// dispatches to the virtual reg_read()/reg_write() hooks that each concrete
// peripheral implements (runtime polymorphism through the vtable).
#pragma once

#include <cstdint>
#include <cstring>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class RegPeripheral : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<RegPeripheral> tsock;

    RegPeripheral(sc_core::sc_module_name nm, uint64_t size, sc_core::sc_time latency)
        : sc_core::sc_module(nm), tsock("tsock"), size_(size), latency_(latency) {
        tsock.register_b_transport(this, &RegPeripheral::b_transport);
    }
    virtual ~RegPeripheral() = default;  // virtual: deleted through base pointers

    uint64_t size() const { return size_; }
    unsigned ignored_accesses() const { return ignored_; }

protected:
    // Return false for an offset that has no register -> TLM address error.
    virtual bool reg_read(uint32_t offset, uint32_t& value) = 0;
    virtual bool reg_write(uint32_t offset, uint32_t value) = 0;

    // Accesses to existing registers with the wrong direction (write to RO,
    // read of WO) are not bus errors on real hardware; they are ignored and
    // counted so tests can assert on them.
    void note_ignored(const char* what, uint32_t offset) {
        ++ignored_;
        SC_REPORT_WARNING(name(), (std::string(what) + " at offset " + std::to_string(offset)).c_str());
    }

private:
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& delay) {
        const uint64_t off = gp.get_address();  // the bus already subtracted our base
        if (gp.get_byte_enable_ptr() || gp.get_streaming_width() != gp.get_data_length()) {
            gp.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (gp.get_data_length() != 4 || (off & 3) || off + 4 > size_) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        uint32_t v = 0;
        bool ok;
        if (gp.is_read()) {
            ok = reg_read(static_cast<uint32_t>(off), v);
            std::memcpy(gp.get_data_ptr(), &v, 4);
        } else if (gp.is_write()) {
            std::memcpy(&v, gp.get_data_ptr(), 4);
            ok = reg_write(static_cast<uint32_t>(off), v);
        } else {
            ok = true;  // TLM_IGNORE_COMMAND
        }
        delay += latency_;
        gp.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }

    uint64_t size_;
    sc_core::sc_time latency_;
    unsigned ignored_ = 0;
};
