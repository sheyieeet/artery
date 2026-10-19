// Deterministic schedules around real INET radios/MACs. No production interposition.
#include "artery/application/CpmService.h"
#include "artery/application/CpService.h"
#include "artery/application/CpObject.h"
#include "artery/application/Middleware.h"
#include "artery/inet/VanetTxControl.h"
#include "artery/networking/GeoNetPacket.h"
#include "artery/utility/IdentityRegistry.h"
#include "inet/common/LayeredProtocolBase.h"
#include "inet/linklayer/ieee80211/mac/Ieee80211Mac.h"
#include "inet/physicallayer/contract/packetlevel/IRadioMedium.h"
#include "inet/physicallayer/contract/packetlevel/ITransmission.h"
#include <omnetpp.h>
#include <vanetza/common/byte_buffer_convertible.hpp>
#include <set>

using namespace omnetpp;

class PrrFixtureService : public artery::CpmService {
    void initialize() override {}
    void finish() override {}
};
Define_Module(PrrFixtureService);

class PrrFixtureMiddleware : public artery::Middleware {
public:
    void equip() {
        Enter_Method_Silent();
        if (!getSubmodule("CP")) {
            auto* cp = cModuleType::get("PrrFixtureService")->createScheduleInit("CP", this);
            (void) cp;
        }
        artery::Identity identity;
        identity.host = getParentModule();
        emit(artery::IdentityRegistry::updateSignal, &identity);
    }
protected:
    void initialize(int stage) override {
        if (stage != 3) return;
        setStationType(getParentModule()->par("rsu").boolValue() ?
            vanetza::geonet::StationType::RSU : vanetza::geonet::StationType::Passenger_Car);
        if (getParentModule()->par("equipped").boolValue()) equip();
    }
    void finish() override {
        artery::Identity identity; identity.host = getParentModule();
        emit(artery::IdentityRegistry::removeSignal, &identity);
    }
};
Define_Module(PrrFixtureMiddleware);

class PrrFixtureDriver : public cSimpleModule {
public:
    void submit() {
        Enter_Method_Silent();
        artery::CpService::VdpSnapshot vdp{};
        // Identical original CPM for every physical transmission and forwarder.
        vdp.stationId = 42;
        vdp.stationType = static_cast<int>(vanetza::geonet::StationType::RSU);
        auto cpm = artery::createCollectivePerceptionMessage(vdp, 0);
        artery::addOriginatingRsuContainer(cpm);
        auto wrapper = std::make_shared<const artery::Cpm>(std::move(cpm));
        auto chunks = std::make_unique<vanetza::ChunkPacket>();
        chunks->layer(vanetza::OsiLayer::Application) = std::unique_ptr<vanetza::convertible::byte_buffer>(
            new vanetza::convertible::byte_buffer_impl<artery::Cpm>(wrapper));
        auto* packet = new artery::GeoNetPacket("identical CPM without POC");
        packet->setPayload(std::move(chunks));
        auto* control = new artery::VanetTxControl;
        control->setDest(inet::MACAddress::BROADCAST_ADDRESS);
        control->setEtherType(0x8947);
        control->setUserPriority(3);
        packet->setControlInfo(control);
        send(packet, "out");
    }
protected:
    void handleMessage(cMessage* message) override { delete message; }
};
Define_Module(PrrFixtureDriver);

class PrrFixtureControl : public cSimpleModule, public cListener {
    std::string test;
    bool acted = false;
    bool duplicating = false;
    long tx = 0, rx = 0;
    double txStartSum = 0.0;
    std::set<int> ids;
    void initialize() override {
        test = getParentModule()->par("testCase").stdstringValue();
        getSystemModule()->subscribe(inet::physicallayer::IRadioMedium::transmissionStartedSignal, this);
        getSystemModule()->subscribe(inet::LayeredProtocolBase::packetSentToUpperSignal, this);
        scheduleAt(SimTime(0.1), new cMessage("submit", 0));
        if (test == "lossfree") for (int i = 1; i < 4; ++i)
            scheduleAt(SimTime(0.1 + i * 0.01), new cMessage("submit", i));
        if (test == "repeated") scheduleAt(SimTime(0.2), new cMessage("submit", 0));
        if (test == "stress") for (int i = 1; i < 1000; ++i)
            scheduleAt(SimTime(0.1 + i * 0.001), new cMessage("submit", 0));
    }
    void handleMessage(cMessage* message) override {
        if (message->isName("submit")) {
            auto* node = getParentModule()->getSubmodule("node", message->getKind());
            check_and_cast<PrrFixtureDriver*>(node->getSubmodule("driver"))->submit();
        } else if (message->isName("cutoff")) {
            delete message;
            endSimulation();
            return;
        } else if (message->isName("enter")) {
            auto* node = getParentModule()->getSubmodule("node", 3);
            check_and_cast<PrrFixtureMiddleware*>(node->getSubmodule("middleware"))->equip();
        } else {
            auto* node = getParentModule()->getSubmodule("node", test == "senderDeparture" ? 0 : 1);
            node->callFinish(); node->deleteModule();
        }
        delete message;
    }
    void receiveSignal(cComponent* source, simsignal_t signal, cObject* value, cObject*) override {
        Enter_Method_Silent();
        if (signal == inet::physicallayer::IRadioMedium::transmissionStartedSignal) {
            const auto* transmission = check_and_cast<const inet::physicallayer::ITransmission*>(value);
            ++tx; ids.insert(transmission->getId());
            txStartSum += transmission->getStartTime().dbl();
            if (!acted && (test == "departure" || test == "senderDeparture" || test == "termination" || test == "entry")) {
                acted = true;
                scheduleAt((transmission->getStartTime() + transmission->getEndTime()) / 2,
                    new cMessage(test == "termination" ? "cutoff" : (test == "entry" ? "enter" : "depart")));
            }
        } else if (test == "corrupt" && dynamic_cast<inet::physicallayer::IRadio*>(source) &&
                check_and_cast<cModule*>(source)->getParentModule()->getParentModule()->getIndex() == 3) {
            check_and_cast<cPacket*>(value)->setBitError(true);
        } else if (dynamic_cast<inet::ieee80211::Ieee80211Mac*>(source) && !duplicating) {
            ++rx;
            if (test == "duplicate") {
                duplicating = true;
                source->emit(signal, value);
                duplicating = false;
            }
        }
    }
    void finish() override {
        recordScalar("independentAirFrames", tx);
        recordScalar("independentUniquePhysicalIds", ids.size());
        recordScalar("independentMacAccepts", rx);
        recordScalar("independentTxStartSum", txStartSum, "s");
        recordScalar("globalRngDraws", getRNG(0)->getNumbersDrawn());
        getSystemModule()->unsubscribe(inet::physicallayer::IRadioMedium::transmissionStartedSignal, this);
        getSystemModule()->unsubscribe(inet::LayeredProtocolBase::packetSentToUpperSignal, this);
    }
};
Define_Module(PrrFixtureControl);
