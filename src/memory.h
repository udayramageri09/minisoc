// Simple RAM model: any length access inside the array, fixed latency.
#pragma once

#include <cstdint>
#include <cstring>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

class Memory : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<Memory> tsock;

    Memory(sc_core::sc_module_name nm, uint64_t size, sc_core::sc_time latency)
        : sc_core::sc_module(nm), tsock("tsock"), mem_(size, 0), latency_(latency) {
        tsock.register_b_transport(this, &Memory::b_transport);
    }

private:
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& delay) {
        const uint64_t addr = gp.get_address();
        const unsigned len = gp.get_data_length();
        if (addr + len > mem_.size()) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.get_byte_enable_ptr()) {
            gp.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (gp.is_read())
            std::memcpy(gp.get_data_ptr(), &mem_[addr], len);
        else if (gp.is_write())
            std::memcpy(&mem_[addr], gp.get_data_ptr(), len);
        delay += latency_;
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    std::vector<uint8_t> mem_;
    sc_core::sc_time latency_;
};
