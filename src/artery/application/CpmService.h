/*
 * Artery V2X Simulation Framework
 * Licensed under GPLv2, see COPYING file for detailed license and warranty terms.
 */

#ifndef ARTERY_CPM_SERVICE_H
#define ARTERY_CPM_SERVICE_H

#include "artery/application/ItsG5BaseService.h"
#include <cstdint>
#include <string>

namespace artery
{

/** Shared application-submission accounting, independent of CPM generation rules. */
class CpmService : public ItsG5BaseService
{
public:
    ~CpmService() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(omnetpp::cMessage*) override;
    void request(const vanetza::btp::DataRequestB&, std::unique_ptr<vanetza::DownPacket>,
        const NetworkInterface* = nullptr);
    virtual void writeMetricToCsv(const std::string&, double, long = 0) {}

private:
    void closeTxWindow(const omnetpp::SimTime& end);
    void closeElapsedTxWindows(const omnetpp::SimTime& now);

    omnetpp::cMessage* mTxWindowTimer = nullptr;
    omnetpp::SimTime mMeasurementStart;
    omnetpp::SimTime mWindowStart;
    omnetpp::SimTime mNextWindowEnd;
    std::uint64_t mWindowBytes = 0;
    std::uint64_t mSubmittedBytes = 0;
    std::uint64_t mSubmissions = 0;
    bool mAccountingFinished = false;
};

} // namespace artery

#endif
