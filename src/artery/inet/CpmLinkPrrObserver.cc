#include "artery/inet/CpmLinkPrrObserver.h"
#include "artery/application/CpmService.h"
#include "artery/application/CpObject.h"
#include "artery/application/Middleware.h"
#include "artery/networking/GeoNetPacket.h"
#include "artery/utility/IdentityRegistry.h"
#include "inet/common/LayeredProtocolBase.h"
#include "inet/linklayer/ieee80211/mac/Ieee80211Mac.h"
#include "inet/physicallayer/base/packetlevel/NarrowbandReceiverBase.h"
#include "inet/physicallayer/contract/packetlevel/IRadioMedium.h"
#include "inet/physicallayer/contract/packetlevel/ITransmission.h"
#include <vanetza/common/byte_buffer_convertible.hpp>
#include <vanetza/net/chunk_packet.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace artery {
using namespace omnetpp;
using namespace inet::physicallayer;
Define_Module(CpmLinkPrrObserver)

namespace {
// Every Artery CPM submission (including rebroadcasts) retains this typed
// application wrapper in GeoNetPacket. Inspect it without encoding, decoding,
// copying the packet, or invoking any reception/error model.
bool isCpm(const cPacket* frame)
{
    for (auto* packet = frame; packet; packet = packet->getEncapsulatedPacket()) {
        const auto* geonet = dynamic_cast<const GeoNetPacket*>(packet);
        if (!geonet || !geonet->hasPayload()) continue;
        const auto* chunks = boost::get<vanetza::ChunkPacket>(&geonet->getPayload());
        if (chunks && dynamic_cast<const vanetza::convertible::byte_buffer_impl<Cpm>*>(
                chunks->layer(vanetza::OsiLayer::Application).ptr())) return true;
    }
    return false;
}

bool hasCpmService(cModule* module)
{
    if (dynamic_cast<CpmService*>(module)) return true;
    for (cModule::SubmoduleIterator it(module); !it.end(); ++it)
        if (hasCpmService(*it)) return true;
    return false;
}

void collectRadios(cModule* module, std::vector<const IRadio*>& radios)
{
    if (auto* radio = dynamic_cast<IRadio*>(module)) radios.push_back(radio);
    for (cModule::SubmoduleIterator it(module); !it.end(); ++it) collectRadios(*it, radios);
}
}

CpmLinkPrrObserver::~CpmLinkPrrObserver()
{
    cancelAndDelete(mTimer);
    // The root outlives this submodule, including dynamic-node destruction.
    if (auto* root = getSimulation()->getSystemModule()) {
        root->unsubscribe(IdentityRegistry::updateSignal, this);
        root->unsubscribe(IdentityRegistry::removeSignal, this);
        root->unsubscribe(IRadioMedium::transmissionStartedSignal, this);
        root->unsubscribe(IRadioMedium::radioRemovedSignal, this);
        root->unsubscribe(inet::LayeredProtocolBase::packetSentToUpperSignal, this);
    }
}

void CpmLinkPrrObserver::initialize()
{
    mTimer = new cMessage("CPM link PRR reception completion");
    // Allow zero-delay radio -> MAC delivery at the reception end first.
    mTimer->setSchedulingPriority(std::numeric_limits<short>::max());
    auto* root = getSystemModule();
    root->subscribe(IdentityRegistry::updateSignal, this);
    root->subscribe(IdentityRegistry::removeSignal, this);
    root->subscribe(IRadioMedium::transmissionStartedSignal, this);
    root->subscribe(IRadioMedium::radioRemovedSignal, this);
    root->subscribe(inet::LayeredProtocolBase::packetSentToUpperSignal, this);
}

void CpmLinkPrrObserver::addStation(cModule* host)
{
    if (!host || mStations.count(host->getId())) return;
    auto* middleware = dynamic_cast<Middleware*>(host->getSubmodule("middleware"));
    if (!middleware || !hasCpmService(middleware)) return;
    Station station;
    station.rsu = middleware->getStationType().getStationType() == vanetza::geonet::StationType::RSU;
    collectRadios(host, station.radios);
    for (const auto* radio : station.radios) mRadioHosts[radio->getId()] = host->getId();
    if (!station.radios.empty()) mStations.emplace(host->getId(), std::move(station));
}

void CpmLinkPrrObserver::removeStation(int host)
{
    const auto station = mStations.find(host);
    if (station == mStations.end()) return;
    for (auto& flight : mFlights) {
        for (auto& receiver : flight.second.receivers) {
            auto& opportunity = receiver.second;
            if (opportunity.outcome == Outcome::Pending && (flight.second.sender == host || receiver.first == host)) {
                // Removal after the reception end cannot undo a completed loss.
                if (opportunity.end < simTime()) {
                    opportunity.outcome = Outcome::Failed;
                    ++counts(opportunity).failed;
                } else {
                    opportunity.outcome = Outcome::CensoredDeparture;
                    ++counts(opportunity).censoredDeparture;
                }
            }
        }
    }
    for (const auto* radio : station->second.radios) mRadioHosts.erase(radio->getId());
    mStations.erase(station);
}

void CpmLinkPrrObserver::startTransmission(const Transmission* transmission)
{
    if (mFlights.count(transmission->getId())) return;
    if (transmission->getStartTime() < getSimulation()->getWarmupPeriod() || !isCpm(transmission->getMacFrame())) return;
    // Counting a fragment as an entire CPM would silently invalidate PRR.
    const auto* frame = dynamic_cast<const inet::ieee80211::Ieee80211DataFrame*>(transmission->getMacFrame());
    if (!frame || frame->getMoreFragments() || frame->getFragmentNumber() != 0)
        throw cRuntimeError("CPM link PRR requires unfragmented IEEE 802.11 CPM frames");
    const auto sender = mRadioHosts.find(transmission->getTransmitter()->getId());
    if (sender == mRadioHosts.end()) throw cRuntimeError("CPM air transmitter is missing from the active CPM station registry");
    const auto* signal = dynamic_cast<const INarrowbandSignal*>(transmission);
    if (!signal) throw cRuntimeError("CPM link PRR requires narrowband channel information");
    ++mAirTransmissions;
    const bool senderRsu = mStations.at(sender->second).rsu;
    const auto position = transmission->getStartPosition();
    Flight flight;
    flight.sender = sender->second;
    for (const auto& entry : mStations) {
        if (entry.first == sender->second) continue;
        double distance = std::numeric_limits<double>::infinity();
        SimTime end;
        for (const auto* radio : entry.second.radios) {
            if (radio->getMedium() != transmission->getTransmitter()->getMedium()) continue;
            const auto* receiver = dynamic_cast<const NarrowbandReceiverBase*>(radio->getReceiver());
            if (!receiver || receiver->getCarrierFrequency() != signal->getCarrierFrequency() ||
                    receiver->getBandwidth() != signal->getBandwidth()) continue;
            const auto receiverPosition = radio->getAntenna()->getMobility()->getCurrentPosition();
            const double d = std::hypot(position.x - receiverPosition.x, position.y - receiverPosition.y);
            if (d > 500.0) continue;
            distance = std::min(distance, d);
            // RadioMedium populated this arrival cache before transmissionStarted.
            // Reading it does not recompute propagation or draw random numbers.
            const auto* arrival = radio->getMedium()->getArrival(radio, transmission);
            if (!arrival) throw cRuntimeError("CPM link PRR requires an existing cached arrival");
            end = std::max(end, arrival->getEndTime());
        }
        if (!std::isfinite(distance)) continue;
        const int bin = std::min(19, static_cast<int>(distance / 25.0));
        const int role = (senderRsu ? 2 : 0) + (entry.second.rsu ? 1 : 0);
        Opportunity opportunity{bin, role, end};
        ++counts(opportunity).eligible;
        flight.receivers.emplace(entry.first, opportunity);
        mExpiry.emplace(end, std::make_pair(transmission->getId(), entry.first));
    }
    if (!flight.receivers.empty()) mFlights.emplace(transmission->getId(), std::move(flight));
    mPeakFlights = std::max(mPeakFlights, mFlights.size());
    mPeakOpportunities = std::max(mPeakOpportunities, mExpiry.size());
    scheduleExpiry();
}

void CpmLinkPrrObserver::radioDelivery(const Radio* radio, const cPacket* frame)
{
    // Radio::endReception emits while receptionTimer still identifies the exact
    // physical transmission. This event alone does NOT establish MAC success.
    const auto* transmission = radio->getReceptionInProgress();
    if (!transmission) return;
    const auto flight = mFlights.find(transmission->getId());
    const auto host = mRadioHosts.find(radio->getId());
    if (flight == mFlights.end() || host == mRadioHosts.end()) return;
    const auto receiver = flight->second.receivers.find(host->second);
    if (receiver == flight->second.receivers.end()) return;
    const auto* module = check_and_cast<const cModule*>(radio);
    const BridgeKey key{module->getParentModule()->getId(), frame->getTreeId()};
    const auto previous = mBridges.find(key);
    if (previous != mBridges.end() && previous->second.time == simTime() && previous->second.transmission != transmission->getId())
        throw cRuntimeError("Ambiguous physical transmission to MAC-frame association");
    mBridges[key] = Bridge{transmission->getId(), host->second, simTime()};
    receiver->second.bridges.push_back(key);
    mPeakBridges = std::max(mPeakBridges, mBridges.size());
}

void CpmLinkPrrObserver::macDelivery(cModule* mac, const cPacket* frame)
{
    const BridgeKey key{mac->getParentModule()->getId(), frame->getTreeId()};
    const auto bridge = mBridges.find(key);
    if (bridge == mBridges.end() || bridge->second.time != simTime()) return;
    const auto flight = mFlights.find(bridge->second.transmission);
    if (flight == mFlights.end()) return;
    const auto receiver = flight->second.receivers.find(bridge->second.receiver);
    if (receiver == flight->second.receivers.end()) return;
    auto& opportunity = receiver->second;
    if (opportunity.outcome == Outcome::Pending) {
        opportunity.outcome = Outcome::Received;
        ++counts(opportunity).received;
    } else if (opportunity.outcome == Outcome::Received) {
        ++mDuplicateCallbacks;
    }
}

void CpmLinkPrrObserver::receiveSignal(cComponent* source, simsignal_t signal, cObject* value, cObject*)
{
    Enter_Method_Silent();
    if (mFinished) return;
    if (signal == IdentityRegistry::updateSignal) {
        if (const auto* identity = dynamic_cast<const Identity*>(value)) addStation(identity->host);
    } else if (signal == IdentityRegistry::removeSignal) {
        if (const auto* identity = dynamic_cast<const Identity*>(value))
            if (identity->host) removeStation(identity->host->getId());
    } else if (signal == IRadioMedium::radioRemovedSignal) {
        const auto* radio = dynamic_cast<const Radio*>(value);
        if (radio) {
            const auto host = mRadioHosts.find(radio->getId());
            if (host != mRadioHosts.end()) {
                const int id = host->second;
                auto& radios = mStations.at(id).radios;
                radios.erase(std::remove(radios.begin(), radios.end(), radio), radios.end());
                mRadioHosts.erase(host);
                if (radios.empty()) removeStation(id);
            }
        }
    } else if (signal == IRadioMedium::transmissionStartedSignal) {
        startTransmission(check_and_cast<const Transmission*>(value));
    } else if (signal == inet::LayeredProtocolBase::packetSentToUpperSignal) {
        if (const auto* frame = dynamic_cast<const cPacket*>(value)) {
            if (const auto* radio = dynamic_cast<const Radio*>(source)) radioDelivery(radio, frame);
            else if (auto* mac = dynamic_cast<inet::ieee80211::Ieee80211Mac*>(source)) macDelivery(mac, frame);
        }
    }
}

void CpmLinkPrrObserver::scheduleExpiry()
{
    if (mExpiry.empty()) return;
    const auto next = mExpiry.begin()->first;
    if (!mTimer->isScheduled() || mTimer->getArrivalTime() != next) {
        cancelEvent(mTimer);
        scheduleAt(next, mTimer);
    }
}

void CpmLinkPrrObserver::expire()
{
    while (!mExpiry.empty() && mExpiry.begin()->first <= simTime()) {
        const auto ids = mExpiry.begin()->second;
        auto flight = mFlights.find(ids.first);
        auto receiver = flight->second.receivers.find(ids.second);
        auto& opportunity = receiver->second;
        if (opportunity.outcome == Outcome::Pending) ++counts(opportunity).failed;
        for (const auto& key : opportunity.bridges) {
            const auto bridge = mBridges.find(key);
            if (bridge != mBridges.end() && bridge->second.transmission == ids.first) mBridges.erase(bridge);
        }
        flight->second.receivers.erase(receiver);
        if (flight->second.receivers.empty()) mFlights.erase(flight);
        mExpiry.erase(mExpiry.begin());
    }
    scheduleExpiry();
}

void CpmLinkPrrObserver::handleMessage(cMessage* message)
{
    if (message != mTimer) throw cRuntimeError("Unexpected CPM PRR observer message");
    expire();
}

void CpmLinkPrrObserver::recordCounts(const std::string& prefix, const Counts& count)
{
    const auto censored = count.censoredDeparture + count.censoredTermination;
    if (count.eligible != count.received + count.failed + censored)
        throw cRuntimeError("CPM link PRR accounting did not conserve transmission/receiver opportunities");
    recordScalar((prefix + ".eligible").c_str(), count.eligible);
    recordScalar((prefix + ".received").c_str(), count.received);
    recordScalar((prefix + ".failed").c_str(), count.failed);
    recordScalar((prefix + ".censoredDeparture").c_str(), count.censoredDeparture);
    recordScalar((prefix + ".censoredTermination").c_str(), count.censoredTermination);
    recordScalar((prefix + ".resolved").c_str(), count.received + count.failed);
    recordScalar((prefix + ".ratio").c_str(), count.received + count.failed ?
        static_cast<double>(count.received) / (count.received + count.failed) : std::numeric_limits<double>::quiet_NaN());
}

void CpmLinkPrrObserver::finish()
{
    if (mFinished) return;
    mFinished = true;
    cancelEvent(mTimer);
    for (auto& flight : mFlights) for (auto& receiver : flight.second.receivers) {
        auto& opportunity = receiver.second;
        if (opportunity.outcome == Outcome::Pending) ++counts(opportunity).censoredTermination;
    }
    Counts total;
    const auto add = [](Counts& a, const Counts& b) {
        a.eligible += b.eligible; a.received += b.received; a.failed += b.failed;
        a.censoredDeparture += b.censoredDeparture; a.censoredTermination += b.censoredTermination;
    };
    const char* roles[] = {"VV", "VR", "RV", "RR"};
    for (int bin = 0; bin < 20; ++bin) {
        Counts pooled;
        const auto distance = ".distance" + std::to_string(bin * 25) + "to" + std::to_string((bin + 1) * 25) + "m";
        for (int role = 0; role < 4; ++role) {
            recordCounts(std::string("cpmLinkPrr.") + roles[role] + distance, mCounts[role][bin]);
            add(pooled, mCounts[role][bin]);
        }
        recordCounts("cpmLinkPrr.all" + distance, pooled);
        add(total, pooled);
    }
    for (int role = 0; role < 4; ++role) {
        Counts pooled;
        for (const auto& bin : mCounts[role]) add(pooled, bin);
        recordCounts(std::string("cpmLinkPrr.") + roles[role] + ".total", pooled);
    }
    recordCounts("cpmLinkPrr.all.total", total);
    recordScalar("cpmLinkPrr.airTransmissions", mAirTransmissions);
    recordScalar("cpmLinkPrr.duplicateMacCallbacks", mDuplicateCallbacks);
    recordScalar("cpmLinkPrr.peakActiveTransmissions", mPeakFlights);
    recordScalar("cpmLinkPrr.peakActiveOpportunities", mPeakOpportunities);
    recordScalar("cpmLinkPrr.peakFrameAssociations", mPeakBridges);
    mFlights.clear(); mExpiry.clear(); mBridges.clear();
}

} // namespace artery
