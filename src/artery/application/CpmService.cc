/*
 * Artery V2X Simulation Framework
 * Licensed under GPLv2, see COPYING file for detailed license and warranty terms.
 */

#include "artery/application/CpmService.h"
#include "artery/application/Asn1PacketVisitor.h"
#include "artery/application/CpObject.h"
#include <algorithm>
#include <limits>
#include <omnetpp/cmessage.h>
#include <vanetza/btp/ports.hpp>

namespace artery
{
using namespace omnetpp;

namespace {
const simsignal_t submittedSignal = cComponent::registerSignal("CpmSubmitted");
const simsignal_t packetSizeSignal = cComponent::registerSignal("cpmPacketSize");
const simsignal_t objectCountSignal = cComponent::registerSignal("CpmTxObjectCount");
const simsignal_t throughputSignal = cComponent::registerSignal("CpmTxThroughput");
const simsignal_t windowBytesSignal = cComponent::registerSignal("CpmTxWindowBytes");
const simsignal_t windowDurationSignal = cComponent::registerSignal("CpmTxWindowDuration");
}

CpmService::~CpmService()
{
    cancelAndDelete(mTxWindowTimer);
}

void CpmService::initialize()
{
    ItsG5BaseService::initialize();
    mMeasurementStart = std::max(simTime(), getSimulation()->getWarmupPeriod());
    mWindowStart = mMeasurementStart;
    mNextWindowEnd = mWindowStart.trunc(SIMTIME_S) + SimTime(1, SIMTIME_S);
    mTxWindowTimer = new cMessage("CPM transmission accounting window");
    // Record boundary-time submissions before closing the window. Algorithm
    // timers retain their existing priority and ordering relative to each other.
    mTxWindowTimer->setSchedulingPriority(std::numeric_limits<short>::max());
    scheduleAt(mNextWindowEnd, mTxWindowTimer);
}

void CpmService::request(const vanetza::btp::DataRequestB& req,
    std::unique_ptr<vanetza::DownPacket> packet, const NetworkInterface* interface)
{
    if (req.destination_port != vanetza::btp::ports::CPM) {
        ItsG5BaseService::request(req, std::move(packet), interface);
        return;
    }

    Asn1PacketVisitor<Cpm> visitor;
    const Cpm* cpm = visitor(*packet);
    if (!cpm) throw cRuntimeError("Cannot account for a submitted CPM without an encoded CPM payload");
    vanetza::ByteBuffer encoded;
    packet->layer(vanetza::OsiLayer::Application).convert(encoded);
    const auto bytes = encoded.size();
    long objects = 0;
    const auto& containers = (*cpm)->payload.cpmContainers.list;
    for (int i = 0; i < containers.count; ++i) {
        const auto* container = containers.array[i];
        if (container->containerId == Vanetza_ITS2_CpmContainerId_perceivedObjectContainer) {
            objects += container->containerData.choice.PerceivedObjectContainer.perceivedObjects.list.count;
        }
    }

    // Count one service-to-middleware submission, including forwarded payloads.
    // Lower-layer duplication, queueing, rejection, or MAC/PHY delivery are
    // separate measurements. Do not change CpmSent or the legacy PRR counters.
    ItsG5BaseService::request(req, std::move(packet), interface);
    closeElapsedTxWindows(simTime());
    if (simTime() >= mMeasurementStart) {
        mWindowBytes += bytes;
        mSubmittedBytes += bytes;
        ++mSubmissions;
    }
    emit(submittedSignal, 1L);
    emit(packetSizeSignal, static_cast<long>(bytes));
    emit(objectCountSignal, objects);
    if (simTime() >= mMeasurementStart) {
        writeMetricToCsv("TX_ObjectCount", objects);
    }
}

void CpmService::closeElapsedTxWindows(const SimTime& now)
{
    // A submission exactly at a boundary belongs to the window ending there.
    while (mNextWindowEnd < now) {
        closeTxWindow(mNextWindowEnd);
        mNextWindowEnd += SimTime(1, SIMTIME_S);
    }
}

void CpmService::closeTxWindow(const SimTime& end)
{
    if (end <= mWindowStart) return;
    const double duration = (end - mWindowStart).dbl();
    const double throughput = mWindowBytes * 8.0 / duration;
    emit(windowBytesSignal, static_cast<long>(mWindowBytes));
    emit(windowDurationSignal, duration);
    emit(throughputSignal, throughput);
    writeMetricToCsv("TX_WindowBytes", mWindowBytes);
    writeMetricToCsv("TX_WindowDuration", duration);
    writeMetricToCsv("TX_Throughput", throughput);
    mWindowBytes = 0;
    mWindowStart = end;
}

void CpmService::handleMessage(cMessage* message)
{
    if (message != mTxWindowTimer) {
        ItsG5BaseService::handleMessage(message);
        return;
    }
    closeTxWindow(mNextWindowEnd);
    mNextWindowEnd += SimTime(1, SIMTIME_S);
    scheduleAt(mNextWindowEnd, mTxWindowTimer);
}

void CpmService::finish()
{
    if (!mAccountingFinished) {
        mAccountingFinished = true;
        cancelEvent(mTxWindowTimer);
        closeElapsedTxWindows(simTime());
        closeTxWindow(simTime());
        const double activeDuration = std::max(0.0, (simTime() - mMeasurementStart).dbl());
        recordScalar("cpmApplicationSubmissions", static_cast<double>(mSubmissions));
        recordScalar("cpmTxPayloadBytes", static_cast<double>(mSubmittedBytes), "B");
        recordScalar("cpmTxActiveDuration", activeDuration, "s");
        if (activeDuration > 0.0) {
            recordScalar("cpmTxAverageThroughput", mSubmittedBytes * 8.0 / activeDuration, "bps");
            // Preserve the conventional statistic name with an exposure-weighted
            // mean instead of averaging unequal partial windows equally.
            recordScalar("cpmTxThroughput:mean", mSubmittedBytes * 8.0 / activeDuration, "bps");
        }
    }
    ItsG5BaseService::finish();
}

} // namespace artery
