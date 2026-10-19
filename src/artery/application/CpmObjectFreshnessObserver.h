#ifndef ARTERY_CPM_OBJECT_FRESHNESS_OBSERVER_H
#define ARTERY_CPM_OBJECT_FRESHNESS_OBSERVER_H

#include "artery/application/CpmObjectProvenance.h"
#include "artery/utility/Geometry.h"
#include <omnetpp.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace artery {
class GlobalEnvironmentModel;
class Middleware;

// One timestamp watermark and one current observation gap, never an event list.
// The watermark survives range exits and warm-up; interval clocks do not.
struct PhysicalObjectFreshnessStream {
    enum Result { Stale, KnowledgeOnly, First, Interval, Coalesced };
    bool known = false;
    omnetpp::SimTime latestInformation;
    bool updated = false;
    omnetpp::SimTime firstArrival;
    omnetpp::SimTime lastArrival;
    std::uint64_t updates = 0;

    void beginEpisode();
    Result observe(omnetpp::SimTime information, omnetpp::SimTime arrival, bool eligible);
};

/** Passive, CPM-only and local/CPM physical-object freshness measurements. */
class CpmObjectFreshnessObserver : public omnetpp::cSimpleModule, public omnetpp::cListener
{
public:
    ~CpmObjectFreshnessObserver() override;
    void attach(const std::shared_ptr<const Cpm>&, std::vector<CpmObjectProvenance>);
    void receiveCpm(omnetpp::cComponent*, const CpObject&);
    CpmObjectProvenance provenanceFor(const std::shared_ptr<const Cpm>&,
        const Vanetza_ITS2_PerceivedObject_t*) const;

protected:
    void initialize() override;
    void handleMessage(omnetpp::cMessage*) override;
    void finish() override;
    void receiveSignal(omnetpp::cComponent*, omnetpp::simsignal_t, omnetpp::cObject*, omnetpp::cObject*) override;
    void receiveSignal(omnetpp::cComponent*, omnetpp::simsignal_t, const char*, omnetpp::cObject*) override;

private:
    using Object = std::weak_ptr<EnvironmentModelObject>;
    using Message = std::weak_ptr<const Cpm>;
    enum EndReason { RangeExit, ObjectDeparture, ReceiverDeparture, Termination };
    struct Pair {
        bool eligible = false;
        bool recording = false;
        omnetpp::SimTime start;
        std::array<PhysicalObjectFreshnessStream, 2> streams;
    };
    struct Receiver {
        bool rsu = false;
        std::string ego;
        Middleware* middleware = nullptr;
        Position position;
        std::map<std::uint64_t, Pair> pairs;
    };
    struct GroundTruth {
        std::uint64_t token;
        std::string externalId;
        Position position;
    };
    struct Statistics {
        std::uint64_t episodes = 0, zeroUpdates = 0, oneUpdate = 0;
        std::uint64_t zeroExposure = 0;
        std::uint64_t updates = 0, intervals = 0, stale = 0, coalesced = 0;
        std::uint64_t initialCompleted = 0, initialCensored = 0, gapCensored = 0;
        double exposure = 0, intervalSum = 0, initialWaitSum = 0, censoredSeconds = 0;
        std::array<std::uint64_t, 4> endings{};
    };

    void addReceiver(omnetpp::cModule*);
    void removeReceiver(int);
    bool receiverPosition(int, Receiver&);
    void refreshGroundTruth();
    void refreshEligibility();
    void observeLocal();
    void observeReceived(int, const CpObject&);
    void observe(Receiver&, Pair&, unsigned mode, omnetpp::SimTime information);
    void beginPair(Pair&);
    void closePair(Receiver&, Pair&, EndReason);
    void startMeasurement();
    void pruneMetadata();
    void updatePeaks();
    void emitSample(unsigned mode, bool rsu, unsigned kind, double);
    void recordStatistics(const std::string&, const Statistics&);

    std::map<int, Receiver> mReceivers;
    std::map<Object, GroundTruth, std::owner_less<Object>> mObjects;
    std::map<Message, std::vector<CpmObjectProvenance>, std::owner_less<Message>> mMetadata;
    std::array<std::array<Statistics, 2>, 2> mStats{}; // CPM/combined, vehicle/RSU
    std::array<std::array<std::array<omnetpp::simsignal_t, 4>, 2>, 2> mSignals{};
    GlobalEnvironmentModel* mEnvironment = nullptr;
    omnetpp::cMessage* mLocalTimer = nullptr;
    omnetpp::cMessage* mMaintenance = nullptr;
    omnetpp::cMessage* mWarmupTimer = nullptr;
    omnetpp::SimTime mWarmup;
    double mRange = 500.0;
    bool mWithLocal = true, mMeasuring = false, mFinished = false;
    std::uint64_t mNextObject = 0, mReceived = 0, mMissingMetadata = 0;
    std::uint64_t mUnresolved = 0, mInactiveObjects = 0, mInvalidTimes = 0;
    std::uint64_t mRegisteredReceivers = 0, mRsuReceivers = 0;
    std::size_t mPeakPairs = 0, mPeakMetadata = 0, mPeakObjects = 0, mPeakMetadataEntries = 0;
    std::size_t mMetadataEntries = 0;
};
} // namespace artery
#endif
