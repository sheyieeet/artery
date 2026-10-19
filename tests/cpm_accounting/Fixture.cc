#include "artery/application/CpService.h"
#include "artery/application/CpObject.h"
#include <omnetpp.h>
#include <vanetza/btp/ports.hpp>
#include <vanetza/common/byte_buffer_convertible.hpp>
#include <numeric>
using namespace omnetpp;
class CpmAccountingFixture : public artery::CpmService {
    void initialize() override {
        CpmService::initialize();
        const std::string kind = par("kind").stdstringValue();
        std::vector<double> times;
        if (kind == "active") times = {0.4,0.5,1.0,2.0,2.2,2.8,3.25};
        if (kind == "late") times = {1.25,2.75};
        if (kind == "terminal") times = {3.9,4.1};
        for (const auto t : times) scheduleAt(SimTime(t), new cMessage("submit"));
    }
    void handleMessage(cMessage* message) override {
        if (!message->isName("submit")) { CpmService::handleMessage(message); return; }
        delete message;
        artery::CpService::VdpSnapshot vdp{};
        vdp.stationId = 42;
        vdp.stationType = static_cast<int>(vanetza::geonet::StationType::RSU);
        auto cpm = artery::createCollectivePerceptionMessage(vdp, 0);
        artery::addOriginatingRsuContainer(cpm);
        const unsigned selected = simTime()==SimTime(2.2) ? 260 : (simTime()==SimTime(2.8) ? 0 : 1);
        if (selected) {
            std::vector<artery::CpService::PerceivedObjectSnapshot> objects(selected);
            std::vector<std::size_t> indices(selected);
            std::iota(indices.begin(),indices.end(),0);
            for (unsigned i=0; i<selected; ++i) { objects[i].cpsId=i; objects[i].hasVelocity=true; objects[i].objectPerceptionQuality=3; objects[i].sensorIds={0}; }
            artery::addPerceivedObjectContainer(cpm,objects,indices,SimTime::ZERO);
        }
        std::string error;
        if (!cpm.validate(error)) throw cRuntimeError("Invalid fixture CPM: %s",error.c_str());
        auto wrapper=std::make_shared<const artery::Cpm>(std::move(cpm));
        auto packet=std::make_unique<vanetza::DownPacket>();
        packet->layer(vanetza::OsiLayer::Application)=std::unique_ptr<vanetza::convertible::byte_buffer>(
            new vanetza::convertible::byte_buffer_impl<artery::Cpm>(wrapper));
        vanetza::btp::DataRequestB req;
        req.destination_port=vanetza::btp::ports::CPM;
        request(req,std::move(packet));
    }
};
Define_Module(CpmAccountingFixture);
class CpmAccountingCoordinator : public cSimpleModule {
    void initialize() override {
        scheduleAt(SimTime(0.25),new cMessage("enter"));
        scheduleAt(SimTime(1.25),new cMessage("late"));
        auto* departure=new cMessage("leave"); departure->setSchedulingPriority(32767);
        scheduleAt(SimTime(3.25),departure);
        auto* lateDeparture=new cMessage("late-leave"); lateDeparture->setSchedulingPriority(32767);
        scheduleAt(SimTime(2.75),lateDeparture);
    }
    void create(const char* name, const char* kind) {
        auto* module=cModuleType::get("CpmAccountingFixture")->create(name,getParentModule());
        module->par("kind")=kind;
        module->finalizeParameters(); module->buildInside(); module->scheduleStart(simTime()); module->callInitialize();
    }
    void remove(const char* name) { auto* module=getParentModule()->getSubmodule(name); module->callFinish(); module->deleteModule(); }
    void handleMessage(cMessage* msg) override {
        if (msg->isName("enter")) { create("active","active"); create("idle","idle"); create("terminal","terminal"); }
        if (msg->isName("late")) create("late","late");
        if (msg->isName("leave")) { remove("active"); remove("idle"); }
        if (msg->isName("late-leave")) remove("late");
        delete msg;
    }
};
Define_Module(CpmAccountingCoordinator);
