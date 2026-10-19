// Test-only aggregate DCC diagnostics. No packet copies, packet logs, or RNG calls.
// The production PRR observer does not install these hooks.
#include "artery/networking/DccEntityBase.h"
#include "artery/application/CpObject.h"
#include <vanetza/common/byte_buffer_convertible.hpp>
#include <omnetpp.h>
#include <dlfcn.h>
#include <map>

namespace {
struct Counts { long releases = 0, cpmReleases = 0, drops = 0; };
std::map<const artery::DccEntityBase*, Counts> counters;
bool cpm(const vanetza::ChunkPacket* packet) {
    return packet && dynamic_cast<const vanetza::convertible::byte_buffer_impl<artery::Cpm>*>(
        packet->layer(vanetza::OsiLayer::Application).ptr());
}
template<class F> F original(const char* symbol) {
    auto* address = dlsym(RTLD_NEXT, symbol);
    if (!address) throw omnetpp::cRuntimeError("DCC test probe cannot resolve %s", symbol);
    return reinterpret_cast<F>(address);
}
}

namespace artery {
void DccEntityBase::initialize(int stage) {
    static auto call = original<void(*)(DccEntityBase*,int)>("_ZN6artery13DccEntityBase10initializeEi");
    call(this,stage);
    if (mFlowControl && !counters.count(this)) {
        auto* count = &counters[this];
        mFlowControl->set_packet_transmit_hook([count](vanetza::access::AccessCategory, const vanetza::ChunkPacket* packet) {
            ++count->releases;
            count->cpmReleases += cpm(packet);
        });
        // Overflow currently supplies the wrong packet pointer. Count drop
        // EVENTS only; never use that pointer to attribute a dropped CPM.
        mFlowControl->set_packet_drop_hook([count](vanetza::access::AccessCategory, const vanetza::ChunkPacket*) {
            ++count->drops;
        });
    }
}
void DccEntityBase::finish() {
    const auto found = counters.find(this);
    if (found != counters.end()) {
        long queued = 0, cpmQueued = 0;
        if (mFlowControl) for (const auto& queue : mFlowControl->m_queues)
            for (const auto& entry : queue.second) { ++queued; cpmQueued += cpm(entry.packet.get()); }
        recordScalar("testDccReleases", found->second.releases);
        recordScalar("testDccCpmReleases", found->second.cpmReleases);
        recordScalar("testDccDropEvents", found->second.drops);
        recordScalar("testDccQueuedAtFinish", queued);
        recordScalar("testDccCpmQueuedAtFinish", cpmQueued);
    }
    static auto call = original<void(*)(DccEntityBase*)>("_ZN6artery13DccEntityBase6finishEv");
    call(this);
    counters.erase(this);
}
}
