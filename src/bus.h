// Address-decoding interconnect (TLM-2.0 router).
//  * map(): registers a target region; overlaps are rejected at elaboration.
//  * b_transport(): binary search over the sorted region table (O(log n)),
//    rebases the address to a target-local offset, forwards, restores it.
//  * Optional CSV transaction trace and fault injection for debugging tests.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

class Bus : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<Bus> tsock;
    tlm_utils::multi_passthrough_initiator_socket<Bus> isock;

    Bus(sc_core::sc_module_name nm, sc_core::sc_time latency)
        : sc_core::sc_module(nm), tsock("tsock"), isock("isock"), latency_(latency) {
        tsock.register_b_transport(this, &Bus::b_transport);
    }

    // Call in the same order as isock.bind(): region i is routed to isock[i].
    void map(const std::string& target, uint64_t base, uint64_t size) {
        Region r{base, size, target, static_cast<int>(order_++)};
        auto it = std::lower_bound(regions_.begin(), regions_.end(), r,
                                   [](const Region& a, const Region& b) { return a.base < b.base; });
        if ((it != regions_.end() && base + size > it->base) ||
            (it != regions_.begin() && std::prev(it)->base + std::prev(it)->size > base))
            throw std::runtime_error("Bus::map: region '" + target + "' overlaps an existing region");
        regions_.insert(it, r);
    }

    void enable_trace(const std::string& path) {
        const auto dir = std::filesystem::path(path).parent_path();
        if (!dir.empty()) std::filesystem::create_directories(dir);
        trace_.open(path);
        if (!trace_) throw std::runtime_error("cannot open trace file " + path);
        trace_ << "time_ns,cmd,addr,data,len,target,status\n";
    }
    void inject_error(uint64_t addr) { faults_.insert(addr); }
    void clear_errors() { faults_.clear(); }

private:
    struct Region {
        uint64_t base, size;
        std::string name;
        int port;
    };

    const Region* decode(uint64_t addr) const {
        // first region with base > addr, then step back one
        auto it = std::upper_bound(regions_.begin(), regions_.end(), addr,
                                   [](uint64_t a, const Region& r) { return a < r.base; });
        if (it == regions_.begin()) return nullptr;
        --it;
        return (addr < it->base + it->size) ? &*it : nullptr;
    }

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& delay) {
        const uint64_t addr = gp.get_address();
        const Region* r = decode(addr);
        delay += latency_;
        if (!r || faults_.count(addr)) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        } else {
            gp.set_address(addr - r->base);
            isock[r->port]->b_transport(gp, delay);
            gp.set_address(addr);  // initiators expect their payload unchanged
        }
        if (trace_.is_open()) {
            uint32_t data = 0;
            std::memcpy(&data, gp.get_data_ptr(), std::min(4u, gp.get_data_length()));
            char buf[160];
            std::snprintf(buf, sizeof buf, "%.0f,%s,0x%08llx,0x%08x,%u,%s,%s\n",
                          (sc_core::sc_time_stamp() + delay).to_seconds() * 1e9, gp.is_read() ? "R" : "W",
                          static_cast<unsigned long long>(addr), data, gp.get_data_length(),
                          r ? r->name.c_str() : "UNMAPPED", gp.get_response_string().c_str());
            trace_ << buf;
        }
    }

    sc_core::sc_time latency_;
    std::vector<Region> regions_;  // kept sorted by base
    unsigned order_ = 0;
    std::set<uint64_t> faults_;
    std::ofstream trace_;
};
