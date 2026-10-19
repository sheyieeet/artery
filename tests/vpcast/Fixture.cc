// Controlled forwarding tests. Only these fixtures replace the network sink;
// production forwarding, expiry, replay handling and submission accounting run.
#include "artery/application/CpServiceVPCast.h"
#include "artery/application/Asn1PacketVisitor.h"
#include "artery/application/NetworkInterface.h"
#include "artery/application/TransportDispatcher.h"
#include "artery/application/VehicleDataProvider.h"
#include "artery/networking/Router.h"
#include <omnetpp.h>
#include <vanetza/common/manual_runtime.hpp>
#include <vanetza/geonet/lifetime.hpp>
#include <dlfcn.h>
#include <functional>
#include <cmath>
#include <vector>
using namespace omnetpp;

namespace {
void require(bool ok, const char* what) { if (!ok) throw cRuntimeError("VP-CAST test: %s",what); }
template<class F> F original(const char* symbol) {
    auto address=dlsym(RTLD_NEXT,symbol);
    if (!address) throw cRuntimeError("Cannot resolve test delegate: %s",symbol);
    return reinterpret_cast<F>(address);
}
bool fixture(const cComponent* c) { return std::string(c->getNedTypeName())=="VpcFixture"; }
struct DummyDcc : artery::IDccEntity {
    void reportLocalChannelLoad(vanetza::dcc::ChannelLoad) override {}
    vanetza::dcc::ChannelProbeProcessor* getChannelProbeProcessor() override { return nullptr; }
    vanetza::dcc::RequestInterface* getRequestInterface() override { return nullptr; }
    vanetza::dcc::TransmitRateThrottle* getTransmitRateThrottle() override { return nullptr; }
    vanetza::geonet::DccFieldGenerator* getGeonetFieldGenerator() override { return nullptr; }
};
class Action : public cMessage {
public:
    explicit Action(std::function<void()> f):cMessage("VP-CAST test action"),run(std::move(f)) {}
    std::function<void()> run;
};
}

class VpcFixtureRouter : public artery::Router {
public:
    vanetza::ManualRuntime runtime;
    ~VpcFixtureRouter() override { mRouter.reset(); } // runtime must outlive the GN router
    int numInitStages() const override { return 1; }
    void initialize(int) override { mRouter=std::make_unique<vanetza::geonet::Router>(runtime,mMIB); }
    void finish() override {}
};
Define_Module(VpcFixtureRouter);

class VpcFixture : public artery::CpServiceVPCast {
public:
    artery::Timer clock;
    artery::VehicleDataProvider vdp{42};
    DummyDcc dcc;
    artery::TransportDispatcher dispatcher;
    std::unique_ptr<artery::NetworkInterface> network;
    VpcFixtureRouter* router=nullptr;
    std::string test;
    unsigned submissions=0,peakSeen=0,peakActive=0;
    std::vector<double> times;
    std::weak_ptr<const artery::Cpm> released;
    bool verified=false;
    double first=0;
    void action(double at,std::function<void()> f) { scheduleAt(SimTime(at),new Action(std::move(f))); }
    std::shared_ptr<const artery::Cpm> message(uint32_t origin=7,int ageMs=0,int variant=0) {
        VdpSnapshot snapshot{};
        snapshot.stationId=origin;
        snapshot.stationType=static_cast<int>(vanetza::geonet::StationType::RSU);
        // CPM origin is deliberately west of ego, opposite the first relay.
        snapshot.referenceLatitude=480000000;
        snapshot.referenceLongitude=109000000;
        auto cpm=artery::createCollectivePerceptionMessage(snapshot,
            artery::countTaiMilliseconds(clock.getCurrentTime())-ageMs);
        artery::addOriginatingRsuContainerVPCast(cpm);
        cpm->payload.managementContainer.referencePosition.altitude.altitudeValue=variant;
        std::string error;
        require(cpm.validate(error),error.c_str());
        return std::make_shared<const artery::Cpm>(std::move(cpm));
    }
    void receive(const std::shared_ptr<const artery::Cpm>& cpm,double east=300,double north=0,double speed=20) {
        vanetza::geonet::LongPositionVector pv;
        pv.gn_addr=vanetza::geonet::Address(vanetza::MacAddress{0,0,0,0,0,9});
        pv.latitude=vanetza::geonet::geo_angle_i32t::from_value(std::lround((48+north/111320)*1e7));
        pv.longitude=vanetza::geonet::geo_angle_i32t::from_value(std::lround((11+east/(111320*std::cos(48*M_PI/180)))*1e7));
        using Speed=vanetza::geonet::LongPositionVector::speed_u15t;
        pv.speed=Speed::from_value(vanetza::BitNumber<uint_fast16_t,15>(std::lround(speed*100)));
        pv.heading=vanetza::geonet::heading_u16t::from_value(900);
        pv.timestamp=vanetza::geonet::Timestamp(vanetza::geonet::Timestamp::time_type::from_value(1+simTime().inUnit(SIMTIME_MS)));
        router->mRouter->m_location_table.update(pv);
        vanetza::btp::DataIndication indication;
        indication.source_position=vanetza::geonet::ShortPositionVector(pv);
        processForwarding(indication,cpm,*network);
        peakSeen=std::max(peakSeen,static_cast<unsigned>(mSeenMessages.size()));
        peakActive=std::max(peakActive,static_cast<unsigned>(mRebroadcastStates.size()));
    }
    void sink(const vanetza::btp::DataRequestB& req,vanetza::DownPacket& packet) {
        artery::Asn1PacketVisitor<artery::Cpm> visitor;
        const auto* cpm=visitor(packet);
        require(cpm!=nullptr,"forwarding lost CPM payload");
        const auto id=identifyMessage(*cpm);
        const auto deadline=messageDeadline(id);
        require(deadline>simTime(),"expired application submission");
        require(req.gn.maximum_lifetime.is_initialized(),"missing GN lifetime");
        const auto lifetime=req.gn.maximum_lifetime->decode();
        require(lifetime.value()<=1,"renewed network lifetime");
        require(SimTime(lifetime.value())<=deadline-simTime(),"GN lifetime exceeds CPM deadline");
        vanetza::ByteBuffer bytes;
        packet.layer(vanetza::OsiLayer::Application).convert(bytes);
        require(bytes.size()==cpm->encode().size() && !bytes.empty(),"encoded size mismatch");
        ++submissions;
        if(test!="stress" && test!="carryStress") times.push_back(simTime().dbl());
    }
    void initialize() override {
        artery::CpmService::initialize();
        initializeForwarding();
        clock.setTimebase("2018-01-01 00:00:00"); mTimer=&clock;
        vdp.setStationType(vanetza::geonet::StationType::Passenger_Car);
        artery::VehicleKinematics kin;
        kin.geo_position.latitude=48*vanetza::units::degree;
        kin.geo_position.longitude=11*vanetza::units::degree;
        kin.heading=vanetza::units::Angle::from_value(M_PI/2);
        kin.speed=10*vanetza::units::si::meter_per_second;
        vdp.update(kin); mVehicleDataProvider=&vdp;
        // Leave legacy serialization snapshot stale. Forwarding must read VDP.
        mVdpSnapshot.referenceLatitude=0;
        mVdpSnapshot.referenceLongitude=0;
        router=check_and_cast<VpcFixtureRouter*>(getParentModule()->getSubmodule("router"));
        network=std::make_unique<artery::NetworkInterface>(*router,dcc,dispatcher);
        test=par("testCase").stdstringValue();
        require(isBehind(-300,0,M_PI/2) && !isBehind(300,0,M_PI/2),"east radians geometry");
        require(isBehind(0,-300,0) && !isBehind(0,300,0),"north geometry");
        require(isBehind(300,0,3*M_PI/2),"west geometry");
        require(std::abs(forwardingWait(0,0,0,30,500,50,.001)-.050)<1e-12,"stationary slots");
        require(std::abs(forwardingWait(30,30,0,30,500,50,.001)-.001)<1e-12,"maximum speeds");
        require(std::abs(forwardingWait(30,0,250,30,500,50,.001)-.01275)<1e-12,"both speeds and factor two");
        require(forwardingWait(30,0,250,30,500,50,.001)==forwardingWait(0,30,250,30,500,50,.001),"symmetric speeds");
        require(mForwardSlots==mForwardRange/par("nominalVehicleSpacing").doubleValue(),"configured range/spacing slot budget");
        action(simTime().dbl()+.1,[this]{ begin(); });
        if(test.substr(0,4)!="live") action(12.1,[this]{ verify(); });
    }
    void begin() {
        if(test=="departure") {
            for(const auto* kind : {"liveDepart","liveDestruct"}) {
                auto* child=cModuleType::get("VpcFixture")->create(kind,getParentModule());
                child->par("testCase")=kind;
                child->finalizeParameters(); child->buildInside(); child->scheduleStart(simTime()); child->callInitialize();
                action(.205,[this,child,kind]{
                    auto* service=check_and_cast<VpcFixture*>(child);
                    require(service->mRebroadcastStates.size()==1,"departure must have live forwarding timer");
                    std::weak_ptr<const artery::Cpm> weak=service->mRebroadcastStates.begin()->second.cpm;
                    if(std::string(kind)=="liveDepart") child->callFinish();
                    child->deleteModule();
                    require(weak.expired(),"departure/destructor retained payload");
                });
            }
            return;
        }
        const int age=test=="late"?1900:(test=="short"?1970:(test=="expired"?2001:(test=="future"?-100:0)));
        auto cpm=message(7,age);
        if(test=="own") rememberOriginated(*cpm);
        receive(cpm);
        released=cpm;
        if(test=="stress" || test=="carryStress") {
            if(test=="stress") receive(cpm,-300);
            for(unsigned i=1;i<10000;++i) action(.1+i*.001,[this,i]{
                auto next=message(100+i); receive(next); if(test=="stress") receive(next,-300); });
            action(10.2,[this]{
                require(peakSeen<=2001,"replay history not age bounded");
                require(peakActive<=2001,"active forwarding cache not age bounded");
            });
            return;
        }
        if(test=="identity") {
            // Old decimal concatenation mapped bytes [1,23] and [12,3] to "123".
            artery::Cpm a(*cpm),b(*cpm);
            asn_uint642INTEGER(&a->payload.managementContainer.referenceTime,279);
            asn_uint642INTEGER(&b->payload.managementContainer.referenceTime,3075);
            require(identifyMessage(a)!=identifyMessage(b),"reference-byte identity collision");
            require(std::get<1>(identifyMessage(a))==279 && std::get<1>(identifyMessage(b))==3075,"full reference time decoding");
            auto second=message(7,0,1);
            require(identifyMessage(*cpm)!=identifyMessage(*second),"same time different CPMs merged");
            receive(second);
            require(mRebroadcastStates.size()==2,"distinct bodies merged");
            receive(cpm,-300); receive(second,-300);
            auto older=message(7,50,2); receive(older);
            require(mRebroadcastStates.size()==1,"valid out-of-order CPM dropped");
            receive(older,-300);
            return;
        }
        if(test=="expiry" || test=="late" || test=="short" || test=="expired" || test=="future") return;
        if(test=="own") { require(mRebroadcastStates.empty(),"own CPM restarted"); return; }
        require(mRebroadcastStates.size()==1,"initial forwarding missing");
        first=.1+forwardingWait(20,10,300,mForwardMaxSpeed,mForwardRange,mForwardSlots,mSlotDuration.dbl());
        if(test=="cancelT" || test=="cleanup") action(.101,[this,cpm]{
            if(test=="cleanup") {
                clearForwarding();
                require(mRebroadcastStates.empty() && mSeenMessages.empty() && mSeenExpiry.empty(),"cleanup retained state");
                require(mForwardingExpiryTimer==nullptr,"cleanup retained expiry timer");
                clearForwarding(); // idempotent departure/destructor path
            } else receive(cpm,-300);
        });
        if(test=="cancelT1") action(first+.001,[this,cpm]{ receive(cpm,-300); });
        if(test=="cancelSCF" || test=="front") action(first+.005,[this,cpm]{ receive(cpm,test=="front"?300:-300); });
        if(test.substr(0,6)=="cancel") {
            action(.5,[this,cpm]{ receive(cpm); require(mRebroadcastStates.empty(),"cancelled CPM restarted before expiry"); });
            action(2.2,[this,cpm]{ receive(cpm); require(mSeenMessages.empty() && mRebroadcastStates.empty(),"expired replay restarted"); });
        }
    }
    void verify() {
        expireForwarding();
        require(mRebroadcastStates.empty() && mSeenMessages.empty() && mSeenExpiry.empty(),"state survived deadline");
        require(released.expired(),"cancelled/expired CPM payload retained");
        unsigned expected=0;
        if(test=="forward" || test=="front" || test=="configured") expected=3;
        if(test=="cancelT1") expected=1;
        if(test=="cancelSCF") expected=2;
        if(test=="late") expected=2;
        if(test=="carryStress") expected=30000;
        require(submissions==expected,"wrong forwarding/retry count");
        require(mSubmissions==submissions,"common submission accounting mismatch");
        if(test=="forward" || test=="front" || test=="configured") {
            require(std::abs(times[0]-first)<1e-6,"initial T: immediate hop/fresh ego/both speeds");
            require(std::abs(times[1]-times[0]-.0025)<1e-9,"SCF entry must send immediately at T1");
            require(std::abs(times[2]-times[1]-1)<1e-9,"SCF retry T2 must be one second");
        }
        recordScalar("vpcastTestPassed",1); recordScalar("testSubmissions",submissions);
        recordScalar("testPeakSeen",peakSeen); recordScalar("testPeakActive",peakActive);
        verified=true;
    }
    void handleMessage(cMessage* message) override {
        if(auto* a=dynamic_cast<Action*>(message)) { auto run=std::move(a->run); delete a; run(); }
        else artery::CpServiceVPCast::handleMessage(message);
    }
    void finish() override {
        require(verified || test=="liveDepart","test did not reach final verification");
        artery::CpServiceVPCast::finish();
        require(mRebroadcastStates.empty() && mSeenMessages.empty() && mForwardingExpiryTimer==nullptr,"finish retained forwarding resources");
    }
    void writeMetricToCsv(const std::string&,double,long=0) override {} // fixture aggregate scalars suffice
};
Define_Module(VpcFixture);

namespace artery {
void ItsG5BaseService::initialize() {
    if(fixture(this)) return;
    static auto call=original<void(*)(ItsG5BaseService*)>("_ZN6artery16ItsG5BaseService10initializeEv"); call(this);
}
void ItsG5BaseService::request(const vanetza::btp::DataRequestB& req,std::unique_ptr<vanetza::DownPacket> packet,const NetworkInterface* interface) {
    if(fixture(this)) { check_and_cast<VpcFixture*>(this)->sink(req,*packet); return; }
    static auto call=original<void(*)(ItsG5BaseService*,const vanetza::btp::DataRequestB&,std::unique_ptr<vanetza::DownPacket>,const NetworkInterface*)>(
        "_ZN6artery16ItsG5BaseService7requestERKN7vanetza3btp12DataRequestBESt10unique_ptrINS1_11ChunkPacketESt14default_deleteIS7_EEPKNS_16NetworkInterfaceE");
    call(this,req,std::move(packet),interface);
}
}
