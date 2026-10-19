#ifndef ARTERY_CPM_LINK_PRR_OBSERVER_H
#define ARTERY_CPM_LINK_PRR_OBSERVER_H

#include <omnetpp.h>
#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace inet { namespace physicallayer { class IRadio; class ITransmission; } }

namespace artery {

/** Passive, physical-transmission/receiver matched CPM link PRR. */
class CpmLinkPrrObserver : public omnetpp::cSimpleModule, public omnetpp::cListener
{
public:
    ~CpmLinkPrrObserver() override;

protected:
    void initialize() override;
    void handleMessage(omnetpp::cMessage*) override;
    void finish() override;
    void receiveSignal(omnetpp::cComponent*, omnetpp::simsignal_t, omnetpp::cObject*, omnetpp::cObject*) override;

private:
    using Radio = inet::physicallayer::IRadio;
    using Transmission = inet::physicallayer::ITransmission;
    using BridgeKey = std::pair<int, long>; // NIC module ID, MAC-frame tree ID
    enum class Outcome { Pending, Received, Failed, CensoredDeparture };
    struct Station {
        bool rsu;
        std::vector<const Radio*> radios;
    };
    struct Opportunity {
        int bin;
        int role;
        omnetpp::SimTime end;
        Outcome outcome = Outcome::Pending;
        std::vector<BridgeKey> bridges;
    };
    struct Flight {
        int sender;
        std::map<int, Opportunity> receivers; // host module ID (station lifetime)
    };
    struct Bridge {
        int transmission;
        int receiver;
        omnetpp::SimTime time;
    };
    struct Counts {
        std::uint64_t eligible = 0;
        std::uint64_t received = 0;
        std::uint64_t failed = 0;
        std::uint64_t censoredDeparture = 0;
        std::uint64_t censoredTermination = 0;
    };

    void addStation(omnetpp::cModule*);
    void removeStation(int);
    void startTransmission(const Transmission*);
    void radioDelivery(const Radio*, const omnetpp::cPacket*);
    void macDelivery(omnetpp::cModule*, const omnetpp::cPacket*);
    void scheduleExpiry();
    void expire();
    void recordCounts(const std::string&, const Counts&);
    Counts& counts(const Opportunity& opportunity) { return mCounts[opportunity.role][opportunity.bin]; }

    std::map<int, Station> mStations;
    std::map<int, int> mRadioHosts;
    std::map<int, Flight> mFlights;
    std::map<BridgeKey, Bridge> mBridges;
    std::multimap<omnetpp::SimTime, std::pair<int, int>> mExpiry;
    std::array<std::array<Counts, 20>, 4> mCounts{};
    omnetpp::cMessage* mTimer = nullptr;
    std::uint64_t mAirTransmissions = 0;
    std::uint64_t mDuplicateCallbacks = 0;
    std::size_t mPeakFlights = 0;
    std::size_t mPeakOpportunities = 0;
    std::size_t mPeakBridges = 0;
    bool mFinished = false;
};

} // namespace artery
#endif
