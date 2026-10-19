#include "artery/application/CpmObjectFreshnessObserver.h"
#include "artery/application/CpmService.h"
#include "artery/application/Middleware.h"
#include "artery/application/VehicleDataProvider.h"
#include "artery/envmod/GlobalEnvironmentModel.h"
#include "artery/envmod/LocalEnvironmentModel.h"
#include "artery/utility/IdentityRegistry.h"
#include "inet/mobility/contract/IMobility.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace artery {
using namespace omnetpp;
Define_Module(CpmObjectFreshnessObserver)

namespace {
CpmObjectFreshnessObserver* observer = nullptr;
const simsignal_t refreshSignal = cComponent::registerSignal("EnvironmentModel.refresh");
const simsignal_t nodeAddSignal = cComponent::registerSignal("traci.node.add");
const simsignal_t nodeRemoveSignal = cComponent::registerSignal("traci.node.remove");

bool hasCpm(cModule* module)
{
    if (dynamic_cast<CpmService*>(module)) return true;
    for (cModule::SubmoduleIterator it(module); !it.end(); ++it)
        if (hasCpm(*it)) return true;
    return false;
}

std::size_t encodedObjects(const Cpm& message)
{
    std::size_t count = 0;
    const auto& containers = message->payload.cpmContainers.list;
    for (int i = 0; i < containers.count; ++i)
        if (containers.array[i]->containerId == Vanetza_ITS2_CpmContainerId_perceivedObjectContainer)
            count += containers.array[i]->containerData.choice.PerceivedObjectContainer.perceivedObjects.list.count;
    return count;
}
}

bool cpmObjectFreshnessEnabled() { return observer != nullptr; }

void observeCpmObjectFreshness(cComponent* source, const CpObject& message)
{
    if (observer) observer->receiveCpm(source, message);
}

CpmObjectProvenance lookupCpmObjectProvenance(const std::shared_ptr<const Cpm>& message,
    const Vanetza_ITS2_PerceivedObject_t* entry)
{
    return observer ? observer->provenanceFor(message, entry) : CpmObjectProvenance{};
}

void publishCpmObjectProvenance(const std::shared_ptr<const Cpm>& message,
    std::vector<CpmObjectProvenance> provenance)
{
    if (observer) observer->attach(message, std::move(provenance));
}

void PhysicalObjectFreshnessStream::beginEpisode()
{
    updated = false;
    updates = 0;
}

PhysicalObjectFreshnessStream::Result PhysicalObjectFreshnessStream::observe(
    SimTime information, SimTime arrival, bool eligible)
{
    if (known && information <= latestInformation) return Stale;
    known = true;
    latestInformation = information;
    if (!eligible) return KnowledgeOnly;
    if (!updated) {
        updated = true;
        updates = 1;
        firstArrival = lastArrival = arrival;
        return First;
    }
    if (arrival == lastArrival) return Coalesced;
    lastArrival = arrival;
    ++updates;
    return Interval;
}

CpmObjectFreshnessObserver::~CpmObjectFreshnessObserver()
{
    if (observer == this) observer = nullptr;
    cancelAndDelete(mLocalTimer);
    cancelAndDelete(mMaintenance);
    cancelAndDelete(mWarmupTimer);
    if (auto* root = getSimulation()->getSystemModule()) {
        root->unsubscribe(IdentityRegistry::updateSignal, this);
        root->unsubscribe(IdentityRegistry::removeSignal, this);
        root->unsubscribe(refreshSignal, this);
        root->unsubscribe(nodeAddSignal, this);
        root->unsubscribe(nodeRemoveSignal, this);
    }
}

void CpmObjectFreshnessObserver::initialize()
{
    if (!par("enabled").boolValue()) { mFinished = true; return; }
    if (observer) throw cRuntimeError("Only one CPM physical-object freshness observer is supported");
    observer = this;
    mRange = par("eligibilityRange").doubleValue();
    if (!std::isfinite(mRange) || mRange <= 0) throw cRuntimeError("Freshness eligibility range must be positive");
    mWithLocal = par("withLocalSensing").boolValue();
    mWarmup = getSimulation()->getWarmupPeriod();
    mMeasuring = simTime() >= mWarmup;
    for (unsigned mode = 0; mode < 2; ++mode) {
        const std::string prefix = mode ? "CombinedPhysicalObject" : "CpmPhysicalObject";
        for (unsigned role = 0; role < 2; ++role) {
            const std::string suffix = role ? "Rsu" : "Vehicle";
            const std::array<std::string, 4> kinds{{"FreshUpdateInterval", "InitialWait", "CensoredInitialWait", "CensoredGap"}};
            for (unsigned kind = 0; kind < 4; ++kind)
                mSignals[mode][role][kind] = registerSignal((prefix + kinds[kind] + suffix).c_str());
        }
    }
    auto* root = getSystemModule();
    root->subscribe(IdentityRegistry::updateSignal, this);
    root->subscribe(IdentityRegistry::removeSignal, this);
    root->subscribe(refreshSignal, this);
    root->subscribe(nodeAddSignal, this);
    root->subscribe(nodeRemoveSignal, this);
    mLocalTimer = new cMessage("physical-object freshness after sensing");
    mMaintenance = new cMessage("physical-object provenance pruning");
    mWarmupTimer = new cMessage("physical-object freshness measurement start");
    for (auto* timer : {mLocalTimer, mMaintenance, mWarmupTimer})
        timer->setSchedulingPriority(std::numeric_limits<short>::max());
    scheduleAt(simTime() + SimTime(1, SIMTIME_S), mMaintenance);
    if (!mMeasuring) scheduleAt(mWarmup, mWarmupTimer);
}

void CpmObjectFreshnessObserver::attach(const std::shared_ptr<const Cpm>& message,
    std::vector<CpmObjectProvenance> provenance)
{
    if (mFinished) return;
    if (encodedObjects(*message) != provenance.size())
        throw cRuntimeError("CPM physical-object provenance does not match actual encoded POC entries");
    if (provenance.empty()) return;
    const Message key(message);
    const auto previous = mMetadata.find(key);
    if (previous != mMetadata.end()) mMetadataEntries -= previous->second.size();
    mMetadataEntries += provenance.size();
    mMetadata[key] = std::move(provenance);
    mPeakMetadata = std::max(mPeakMetadata, mMetadata.size());
    mPeakMetadataEntries = std::max(mPeakMetadataEntries, mMetadataEntries);
}

void CpmObjectFreshnessObserver::addReceiver(cModule* host)
{
    if (!host) return;
    auto* middleware = dynamic_cast<Middleware*>(host->getSubmodule("middleware"));
    if (!middleware || !hasCpm(middleware)) return;
    auto found = mReceivers.find(host->getId());
    if (found != mReceivers.end()) {
        found->second.ego = middleware->getIdentity().traci;
        return;
    }
    Receiver receiver;
    receiver.middleware = middleware;
    receiver.ego = middleware->getIdentity().traci;
    receiver.rsu = middleware->getStationType().getStationType() == vanetza::geonet::StationType::RSU;
    ++mRegisteredReceivers;
    if (receiver.rsu) ++mRsuReceivers;
    mReceivers.emplace(host->getId(), std::move(receiver));
}

CpmObjectProvenance CpmObjectFreshnessObserver::provenanceFor(
    const std::shared_ptr<const Cpm>& message, const Vanetza_ITS2_PerceivedObject_t* entry) const
{
    const auto metadata = mMetadata.find(Message(message));
    if (metadata == mMetadata.end()) return {};
    std::size_t index = 0;
    const auto& containers = (*message)->payload.cpmContainers.list;
    for (int i = 0; i < containers.count; ++i) {
        if (containers.array[i]->containerId != Vanetza_ITS2_CpmContainerId_perceivedObjectContainer) continue;
        const auto& objects = containers.array[i]->containerData.choice.PerceivedObjectContainer.perceivedObjects.list;
        for (int j = 0; j < objects.count; ++j, ++index)
            if (objects.array[j] == entry) return index < metadata->second.size() ? metadata->second[index] : CpmObjectProvenance{};
    }
    return {};
}

void CpmObjectFreshnessObserver::removeReceiver(int id)
{
    const auto found = mReceivers.find(id);
    if (found == mReceivers.end()) return;
    for (auto& entry : found->second.pairs) closePair(found->second, entry.second, ReceiverDeparture);
    mReceivers.erase(found);
}

bool CpmObjectFreshnessObserver::receiverPosition(int id, Receiver& receiver)
{
    auto* host = getSimulation()->getModule(id);
    if (!host) return false;
    const auto* vdp = receiver.middleware->getFacilities().get_const_ptr<VehicleDataProvider>();
    if (vdp) receiver.position = vdp->position();
    else {
        auto* mobility = dynamic_cast<inet::IMobility*>(host->getSubmodule("mobility"));
        if (!mobility) return false;
        const auto pos = mobility->getCurrentPosition();
        receiver.position = Position(pos.x, pos.y);
    }
    return std::isfinite(receiver.position.x.value()) && std::isfinite(receiver.position.y.value());
}

void CpmObjectFreshnessObserver::refreshGroundTruth()
{
    if (!mEnvironment) return;
    std::set<Object, std::owner_less<Object>> active;
    for (const auto& object : mEnvironment->getAllObjects()) {
        try {
            const Object key(object);
            const auto pos = object->getCentrePoint();
            if (!std::isfinite(pos.x.value()) || !std::isfinite(pos.y.value())) continue;
            active.insert(key);
            auto found = mObjects.find(key);
            if (found == mObjects.end())
                mObjects.emplace(key, GroundTruth{++mNextObject, object->getExternalId(), pos});
            else found->second.position = pos;
        } catch (...) { /* Removed TraCI objects cannot form eligible pairs. */ }
    }
    for (auto it = mObjects.begin(); it != mObjects.end();) {
        if (!active.count(it->first)) {
            for (auto& receiver : mReceivers) {
                auto pair = receiver.second.pairs.find(it->second.token);
                if (pair != receiver.second.pairs.end()) {
                    closePair(receiver.second, pair->second, ObjectDeparture);
                    receiver.second.pairs.erase(pair);
                }
            }
            it = mObjects.erase(it);
        } else ++it;
    }
    mPeakObjects = std::max(mPeakObjects, mObjects.size());
    refreshEligibility();
}

void CpmObjectFreshnessObserver::beginPair(Pair& pair)
{
    pair.eligible = true;
    pair.recording = mMeasuring;
    pair.start = simTime();
    for (auto& stream : pair.streams) stream.beginEpisode();
}

void CpmObjectFreshnessObserver::refreshEligibility()
{
    // A spatial grid avoids a receiver-by-whole-world scan on each TraCI step.
    using Cell = std::pair<long, long>;
    std::map<Cell, std::vector<const GroundTruth*>> grid;
    const auto cell = [this](const Position& p) {
        return Cell{static_cast<long>(std::floor(p.x.value() / mRange)),
            static_cast<long>(std::floor(p.y.value() / mRange))};
    };
    for (const auto& object : mObjects) grid[cell(object.second.position)].push_back(&object.second);
    for (auto& item : mReceivers) {
        auto& receiver = item.second;
        std::set<std::uint64_t> eligible;
        if (receiverPosition(item.first, receiver)) {
            const auto centre = cell(receiver.position);
            for (long dx = -1; dx <= 1; ++dx) for (long dy = -1; dy <= 1; ++dy) {
                const auto found = grid.find({centre.first + dx, centre.second + dy});
                if (found == grid.end()) continue;
                for (const auto* object : found->second) {
                    if (!receiver.ego.empty() && object->externalId == receiver.ego) continue;
                    const double d = distance(receiver.position, object->position).value();
                    if (d > 0.0 && d <= mRange) eligible.insert(object->token);
                }
            }
        }
        for (auto& entry : receiver.pairs)
            if (entry.second.eligible && !eligible.count(entry.first)) closePair(receiver, entry.second, RangeExit);
        for (const auto token : eligible) {
            auto& pair = receiver.pairs[token];
            if (!pair.eligible) beginPair(pair);
        }
    }
    updatePeaks();
}

void CpmObjectFreshnessObserver::startMeasurement()
{
    if (mMeasuring || simTime() < mWarmup) return;
    mMeasuring = true;
    for (auto& receiver : mReceivers) for (auto& entry : receiver.second.pairs)
        if (entry.second.eligible) beginPair(entry.second); // keep pre-warm-up knowledge
}

void CpmObjectFreshnessObserver::observe(Receiver& receiver, Pair& pair, unsigned mode, SimTime information)
{
    if (information < SimTime::ZERO || information > simTime()) { ++mInvalidTimes; return; }
    auto& stream = pair.streams[mode];
    const auto previous = stream.lastArrival;
    const auto result = stream.observe(information, simTime(), pair.eligible && pair.recording);
    auto& stats = mStats[mode][receiver.rsu];
    if (result == PhysicalObjectFreshnessStream::Stale) {
        if (pair.recording) ++stats.stale;
    } else if (result == PhysicalObjectFreshnessStream::Coalesced) {
        ++stats.coalesced;
    } else if (result == PhysicalObjectFreshnessStream::First) {
        ++stats.updates;
        ++stats.initialCompleted;
        const double wait = (simTime() - pair.start).dbl();
        stats.initialWaitSum += wait;
        emitSample(mode, receiver.rsu, 1, wait);
    } else if (result == PhysicalObjectFreshnessStream::Interval) {
        ++stats.updates;
        ++stats.intervals;
        const double interval = (simTime() - previous).dbl();
        stats.intervalSum += interval;
        emitSample(mode, receiver.rsu, 0, interval);
    }
}

void CpmObjectFreshnessObserver::observeReceived(int id, const CpObject& object)
{
    startMeasurement();
    const auto receiver = mReceivers.find(id);
    if (receiver == mReceivers.end()) return;
    const auto message = object.shared_ptr();
    const auto count = encodedObjects(*message);
    if (!count) return;
    ++mReceived;
    const auto metadata = mMetadata.find(Message(message));
    if (metadata == mMetadata.end()) { mMissingMetadata += count; return; }
    for (const auto& provenance : metadata->second) {
        if (!provenance.known) { ++mUnresolved; continue; }
        const auto target = mObjects.find(provenance.object);
        if (target == mObjects.end()) { ++mInactiveObjects; continue; }
        if (!receiver->second.ego.empty() && target->second.externalId == receiver->second.ego) continue;
        auto& pair = receiver->second.pairs[target->second.token];
        observe(receiver->second, pair, 0, provenance.informationTime);
        if (mWithLocal) observe(receiver->second, pair, 1, provenance.informationTime);
    }
    updatePeaks();
}

void CpmObjectFreshnessObserver::receiveCpm(cComponent* source, const CpObject& message)
{
    Enter_Method_Silent();
    if (mFinished) return;
    auto* module = dynamic_cast<cModule*>(source);
    while (module && !mReceivers.count(module->getId())) module = module->getParentModule();
    if (module) observeReceived(module->getId(), message);
}

void CpmObjectFreshnessObserver::observeLocal()
{
    if (!mWithLocal) return;
    startMeasurement();
    for (auto& receiver : mReceivers) {
        auto* local = receiver.second.middleware->getFacilities().get_mutable_ptr<LocalEnvironmentModel>();
        if (!local) continue;
        for (const auto& tracked : local->allObjects()) {
            const auto target = mObjects.find(tracked.first);
            if (target == mObjects.end()) continue;
            if (!receiver.second.ego.empty() && target->second.externalId == receiver.second.ego) continue;
            bool haveTime = false;
            SimTime last;
            for (const auto& sensor : tracked.second.sensors()) {
                if (!haveTime || sensor.second.last() > last) last = sensor.second.last();
                haveTime = true;
            }
            if (haveTime) observe(receiver.second, receiver.second.pairs[target->second.token], 1, last);
        }
    }
    updatePeaks();
}

void CpmObjectFreshnessObserver::closePair(Receiver& receiver, Pair& pair, EndReason reason)
{
    if (!pair.eligible) return;
    if (pair.recording && simTime() >= pair.start) {
        for (unsigned mode = 0; mode < (mWithLocal ? 2u : 1u); ++mode) {
            const auto& stream = pair.streams[mode];
            auto& stats = mStats[mode][receiver.rsu];
            ++stats.episodes;
            if (simTime() == pair.start) ++stats.zeroExposure;
            ++stats.endings[reason];
            stats.exposure += (simTime() - pair.start).dbl();
            if (!stream.updated) ++stats.zeroUpdates;
            else if (stream.updates == 1) ++stats.oneUpdate;
            const double gap = (simTime() - (stream.updated ? stream.lastArrival : pair.start)).dbl();
            if (!stream.updated || gap > 0) {
                if (stream.updated) ++stats.gapCensored;
                else ++stats.initialCensored;
                stats.censoredSeconds += gap;
                emitSample(mode, receiver.rsu, stream.updated ? 3 : 2, gap);
            }
        }
    }
    pair.eligible = pair.recording = false;
    for (auto& stream : pair.streams) stream.beginEpisode();
}

void CpmObjectFreshnessObserver::emitSample(unsigned mode, bool rsu, unsigned kind, double value)
{
    emit(mSignals[mode][rsu][kind], value);
}

void CpmObjectFreshnessObserver::pruneMetadata()
{
    for (auto it = mMetadata.begin(); it != mMetadata.end();) {
        if (it->first.expired()) {
            mMetadataEntries -= it->second.size();
            it = mMetadata.erase(it);
        } else ++it;
    }
}

void CpmObjectFreshnessObserver::updatePeaks()
{
    std::size_t pairs = 0;
    for (const auto& receiver : mReceivers) pairs += receiver.second.pairs.size();
    mPeakPairs = std::max(mPeakPairs, pairs);
}

void CpmObjectFreshnessObserver::receiveSignal(cComponent* source, simsignal_t signal, cObject* value, cObject*)
{
    Enter_Method_Silent();
    if (mFinished) return;
    if (signal == IdentityRegistry::updateSignal) {
        if (const auto* identity = dynamic_cast<const Identity*>(value)) addReceiver(identity->host);
    } else if (signal == IdentityRegistry::removeSignal) {
        if (const auto* identity = dynamic_cast<const Identity*>(value))
            if (identity->host) removeReceiver(identity->host->getId());
    } else if (signal == refreshSignal) {
        mEnvironment = dynamic_cast<GlobalEnvironmentModel*>(source);
        startMeasurement();
        refreshGroundTruth();
        if (!mLocalTimer->isScheduled()) scheduleAt(simTime(), mLocalTimer);
    }
}

void CpmObjectFreshnessObserver::handleMessage(cMessage* message)
{
    if (message == mLocalTimer) observeLocal();
    else if (message == mWarmupTimer) startMeasurement();
    else if (message == mMaintenance) {
        pruneMetadata();
        scheduleAt(simTime() + SimTime(1, SIMTIME_S), mMaintenance);
    } else throw cRuntimeError("Unexpected physical-object freshness timer");
}

void CpmObjectFreshnessObserver::receiveSignal(cComponent*, simsignal_t signal, const char*, cObject*)
{
    Enter_Method_Silent();
    if (!mFinished && (signal == nodeAddSignal || signal == nodeRemoveSignal)) {
        startMeasurement();
        refreshGroundTruth();
    }
}

void CpmObjectFreshnessObserver::recordStatistics(const std::string& prefix, const Statistics& s)
{
    const auto record = [this, &prefix](const char* suffix, double value) {
        recordScalar((prefix + "." + suffix).c_str(), value);
    };
    record("eligibleEpisodes", s.episodes); record("eligibleSeconds", s.exposure);
    record("zeroExposureEpisodes", s.zeroExposure);
    record("zeroUpdateEpisodes", s.zeroUpdates); record("oneUpdateEpisodes", s.oneUpdate);
    record("freshUpdates", s.updates); record("completedIntervals", s.intervals);
    record("completedIntervalSeconds", s.intervalSum);
    record("completedInitialWaits", s.initialCompleted); record("completedInitialWaitSeconds", s.initialWaitSum);
    record("censoredInitialWaits", s.initialCensored); record("censoredGaps", s.gapCensored);
    record("censoredSeconds", s.censoredSeconds); record("staleInformation", s.stale);
    record("coalescedUpdates", s.coalesced);
    record("rangeExitEpisodes", s.endings[RangeExit]); record("objectDepartureEpisodes", s.endings[ObjectDeparture]);
    record("receiverDepartureEpisodes", s.endings[ReceiverDeparture]); record("terminationEpisodes", s.endings[Termination]);
}

void CpmObjectFreshnessObserver::finish()
{
    if (mFinished) return;
    mFinished = true;
    for (auto* timer : {mLocalTimer, mMaintenance, mWarmupTimer}) cancelEvent(timer);
    for (auto& receiver : mReceivers) for (auto& entry : receiver.second.pairs)
        closePair(receiver.second, entry.second, Termination);
    for (unsigned mode = 0; mode < (mWithLocal ? 2u : 1u); ++mode)
        for (unsigned role = 0; role < 2; ++role)
            recordStatistics(std::string("objectFreshness.") + (mode ? "combined." : "cpmOnly.") +
                (role ? "rsu" : "vehicle"), mStats[mode][role]);
    recordScalar("objectFreshness.receivedObjectCpms", mReceived);
    recordScalar("objectFreshness.missingMetadataEntries", mMissingMetadata);
    recordScalar("objectFreshness.unresolvedProvenanceEntries", mUnresolved);
    recordScalar("objectFreshness.inactiveObjectEntries", mInactiveObjects);
    recordScalar("objectFreshness.invalidInformationTimes", mInvalidTimes);
    recordScalar("objectFreshness.registeredReceivers", mRegisteredReceivers);
    recordScalar("objectFreshness.rsuReceivers", mRsuReceivers);
    recordScalar("objectFreshness.peakPairStates", mPeakPairs);
    recordScalar("objectFreshness.peakGroundTruthObjects", mPeakObjects);
    recordScalar("objectFreshness.peakMetadataMessages", mPeakMetadata);
    recordScalar("objectFreshness.peakMetadataEntries", mPeakMetadataEntries);
    mReceivers.clear(); mObjects.clear(); mMetadata.clear(); mMetadataEntries = 0;
}
} // namespace artery
