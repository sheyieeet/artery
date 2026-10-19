// Read-only integration observer. The fixture alone uses a test transmission sink.
#include "artery/application/CpService.h"
#include "artery/application/Asn1PacketVisitor.h"
#include "artery/application/VehicleDataProvider.h"
#include "artery/utility/Identity.h"
#include <omnetpp.h>
#include <dlfcn.h>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vanetza/btp/ports.hpp>
using namespace omnetpp;
namespace {
std::ofstream& out() { static auto* f = new std::ofstream(std::getenv("CPM_ACCOUNTING_TRACE")); return *f; }
template<class F> F original(const char* symbol) {
    auto* address = dlsym(RTLD_NEXT, symbol);
    if (!address) throw cRuntimeError("Test observer cannot resolve %s: %s", symbol, dlerror());
    return reinterpret_cast<F>(address);
}
bool fixture(cComponent* module) { return std::string(module->getNedTypeName()) == "CpmAccountingFixture"; }
void prefix(const char* event, cComponent* module) {
    out() << std::setprecision(16) << "{\"event\":\"" << event << "\",\"module\":\""
          << module->getFullPath() << "\",\"t\":" << simTime().dbl();
}
class Listener : public cListener {
    void receiveSignal(cComponent* source, simsignal_t signal, long value, cObject*) override {
        prefix(cComponent::getSignalName(signal), source); out() << ",\"value\":" << value << "}" << std::endl;
    }
    void receiveSignal(cComponent* source, simsignal_t signal, double value, cObject*) override {
        prefix(cComponent::getSignalName(signal), source); out() << ",\"value\":" << value << "}" << std::endl;
    }
};
}
namespace artery {
void ItsG5BaseService::initialize() {
    if (fixture(this)) return;
    static auto call = original<void(*)(ItsG5BaseService*)>("_ZN6artery16ItsG5BaseService10initializeEv");
    call(this);
}
void CpmService::initialize() {
    static auto call = original<void(*)(CpmService*)>("_ZN6artery10CpmService10initializeEv");
    call(this);
    static auto* listener = new Listener;
    for (const auto* signal : {"CpmSubmitted", "cpmPacketSize", "CpmTxObjectCount", "CpmTxThroughput", "CpmTxWindowBytes", "CpmTxWindowDuration"})
        cComponent::subscribe(signal, listener);
    prefix("start", this); out() << ",\"warmup\":" << getSimulation()->getWarmupPeriod().dbl() << "}" << std::endl;
}
void CpmService::finish() {
    static auto call = original<void(*)(CpmService*)>("_ZN6artery10CpmService6finishEv");
    call(this);
    prefix("finish", this); out() << "}" << std::endl;
}
void ItsG5BaseService::request(const vanetza::btp::DataRequestB& req, std::unique_ptr<vanetza::DownPacket> packet, const NetworkInterface* interface) {
    if (req.destination_port == vanetza::btp::ports::CPM) {
        vanetza::ByteBuffer bytes;
        packet->layer(vanetza::OsiLayer::Application).convert(bytes);
        Cpm decoded;
        const bool decodedWire = decoded.decode(bytes);
        Asn1PacketVisitor<Cpm> visitor;
        const Cpm* inspected = decodedWire ? &decoded : visitor(*packet);
        if (!decodedWire && !fixture(this)) throw cRuntimeError("Test cannot decode submitted wire payload");
        long objects = 0;
        std::set<long> ids;
        const auto& list = (*inspected)->payload.cpmContainers.list;
        for (int i=0; i<list.count; ++i) if (list.array[i]->containerId == Vanetza_ITS2_CpmContainerId_perceivedObjectContainer) {
            const auto& pos = list.array[i]->containerData.choice.PerceivedObjectContainer.perceivedObjects.list;
            objects += pos.count;
            for (int j=0; j<pos.count; ++j) ids.insert(*pos.array[j]->objectId);
        }
        // The bundled ASN.1 decoder cannot round-trip this 255-object synthetic
        // payload. For that stress case, inspect the validated encoder input;
        // all real-network payloads must still pass independent wire decoding.
        if (!decodedWire && objects != 255) throw cRuntimeError("Unexpected fixture decoding failure");
        bool forwarded = false;
        if (!fixture(this)) {
            const auto* vdp = getFacilities().get_const_ptr<VehicleDataProvider>();
            const auto* identity = getFacilities().get_const_ptr<Identity>();
            const auto station = vdp ? vdp->getStationId() : identity->application;
            forwarded = station != decoded->header.stationId;
        }
        prefix("request", this);
        out() << ",\"bytes\":" << bytes.size() << ",\"objects\":" << objects
              << ",\"forwarded\":" << (forwarded ? "true" : "false")
              << ",\"wire_decoded\":" << (decodedWire ? "true" : "false") << ",\"ids\":[";
        bool first=true; for (auto id : ids) { if (!first) out() << ','; first=false; out() << id; }
        out() << "]}" << std::endl;
    }
    if (fixture(this)) return; // Explicit sink for deterministic accounting tests only.
    static auto call = original<void(*)(ItsG5BaseService*, const vanetza::btp::DataRequestB&, std::unique_ptr<vanetza::DownPacket>, const NetworkInterface*)>(
        "_ZN6artery16ItsG5BaseService7requestERKN7vanetza3btp12DataRequestBESt10unique_ptrINS1_11ChunkPacketESt14default_deleteIS7_EEPKNS_16NetworkInterfaceE");
    call(this, req, std::move(packet), interface);
}
void CpService::checkTriggeringConditions(const SimTime& now) {
    static auto call = original<void(*)(CpService*, const SimTime&)>("_ZN6artery9CpService25checkTriggeringConditionsERKN7omnetpp7SimTimeE");
    const auto before = mLastCpmDecisionTimestamp;
    const auto txBefore = mLastCpmTimestamp;
    call(this, now);
    if (mUseProposedGenerationRules && before != mLastCpmDecisionTimestamp) {
        prefix("decision", this);
        out() << ",\"previous\":" << before.dbl() << ",\"sent\":" << (mLastCpmTimestamp != txBefore ? "true" : "false")
              << ",\"history\":" << mReceivedObjectsHistory.size()
              << ",\"local_objects\":" << mPerceivedObjectSnapshot.size() << ",\"ids\":[";
        bool first=true; for (const auto i : mSelectedCpmObjects) { if (!first) out() << ','; first=false; out() << mPerceivedObjectSnapshot[i].cpsId; }
        out() << "]}" << std::endl;
    }
}
}
