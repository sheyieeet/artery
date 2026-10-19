// Production forwarding and accounting with controlled CPMs and a fixture-only
// network sink. Random samples still consume the original global RNG stream;
// selected cases then substitute a known result to exercise exact boundaries.
#include "artery/application/CpServiceWeightedPPersistence.h"
#include "artery/application/Asn1PacketVisitor.h"
#include "artery/application/VehicleDataProvider.h"
#include <omnetpp.h>
#include <dlfcn.h>
#include <cmath>
#include <functional>
#include <vector>
using namespace omnetpp;

namespace {
void require(bool value,const char* what) { if(!value) throw cRuntimeError("Weighted p-persistence test: %s",what); }
template<class F> F original(const char* symbol) {
    auto address=dlsym(RTLD_NEXT,symbol);
    if(!address) throw cRuntimeError("Cannot resolve weighted test delegate: %s",symbol);
    return reinterpret_cast<F>(address);
}
bool fixture(const cComponent* c) { return std::string(c->getNedTypeName())=="WeightedFixture"; }
class Action : public cMessage {
public:
    explicit Action(std::function<void()> f):cMessage("Weighted test action"),run(std::move(f)) {}
    std::function<void()> run;
};
}

class WeightedFixture : public artery::CpServiceWeightedPPersistence {
public:
    artery::Timer clock;
    artery::VehicleDataProvider vdp{42};
    artery::GeoPosition staticPosition;
    std::string test;
    unsigned submissions=0,draws=0,initials=0,fallbacks=0,peakSeen=0,peakActive=0;
    unsigned long rngStart=0;
    bool verified=false;
    double latitude=48,longitude=11;
    std::vector<double> times;
    std::weak_ptr<const artery::Cpm> released;
    vanetza::ByteBuffer replayBytes;
    bool stress() const { return test=="stress" || test=="fallbackStress" || test=="randomStress"; }
    void action(double at,std::function<void()> f) { scheduleAt(SimTime(at),new Action(std::move(f))); }
    std::shared_ptr<const artery::Cpm> message(uint32_t origin=7,int ageMs=0,int variant=0) {
        VdpSnapshot snapshot{};
        snapshot.stationId=origin;
        snapshot.stationType=static_cast<int>(vanetza::geonet::StationType::RSU);
        // Fixed original location deliberately differs from every last-hop test.
        snapshot.referenceLatitude=480000000;
        snapshot.referenceLongitude=109000000;
        auto cpm=artery::createCollectivePerceptionMessageWeightedPP(snapshot,
            artery::countTaiMilliseconds(clock.getCurrentTime())-ageMs);
        artery::addOriginatingRsuContainerWeightedPP(cpm);
        cpm->payload.managementContainer.referencePosition.altitude.altitudeValue=variant;
        std::string error;
        require(cpm.validate(error),error.c_str());
        return std::make_shared<const artery::Cpm>(std::move(cpm));
    }
    std::shared_ptr<const artery::Cpm> replay() {
        artery::Cpm cpm;
        require(cpm.decode(replayBytes),"cannot decode replay fixture");
        return std::make_shared<const artery::Cpm>(std::move(cpm));
    }
    void receive(const std::shared_ptr<const artery::Cpm>& cpm,double east=600,bool invalidPosition=false) {
        vanetza::btp::DataIndication ind;
        ind.source_position.latitude=vanetza::geonet::geo_angle_i32t::from_value(
            invalidPosition?900000001:std::lround(latitude*1e7));
        const double lon=std::remainder(longitude+east/(111320*std::cos(latitude*M_PI/180)),360.0);
        ind.source_position.longitude=vanetza::geonet::geo_angle_i32t::from_value(std::lround(lon*1e7));
        processForwarding(ind,cpm);
        peakSeen=std::max(peakSeen,static_cast<unsigned>(mSeenMessages.size()));
        peakActive=std::max(peakActive,static_cast<unsigned>(mBufferedMessages.size()));
    }
    double sample(cRNG* rng,double a,double b,double actual) {
        require(rng==getEnvir()->getRNG(0) && a==0 && b==1,"changed probability RNG mapping");
        ++draws;
        if(test=="randomStress") return actual;
        if(test=="zero" || test=="cancel" || test=="expiryFallback" || test=="fallbackStress" || test.substr(0,4)=="live") return 0;
        if(test=="equalProbability") {
            require(mBufferedMessages.size()==1,"missing probability state");
            return mBufferedMessages.begin()->second.minDij/mTransmissionRange;
        }
        if(test=="probability") return std::nextafter(1.0,0.0);
        return .5;
    }
    void sink(const vanetza::btp::DataRequestB& req,vanetza::DownPacket& packet) {
        artery::Asn1PacketVisitor<artery::Cpm> visitor;
        const auto* cpm=visitor(packet);
        require(cpm!=nullptr,"forwarded payload missing");
        const auto id=identifyMessage(*cpm);
        const auto deadline=messageDeadline(id);
        require(deadline>simTime(),"expired CPM submitted");
        require(req.gn.maximum_lifetime.is_initialized(),"missing GN lifetime");
        const SimTime lifetime(req.gn.maximum_lifetime->decode().value());
        const auto remaining=std::min(SimTime(1),deadline-simTime());
        const auto expected=SimTime::fromRaw((remaining.raw()/SimTime(.05).raw())*SimTime(.05).raw());
        require(lifetime==expected && lifetime>SimTime::ZERO,"GN lifetime not rounded down to remaining age");
        vanetza::ByteBuffer bytes;
        packet.layer(vanetza::OsiLayer::Application).convert(bytes);
        require(!bytes.empty() && bytes.size()==cpm->encode().size(),"encoded size mismatch");
        const auto active=mBufferedMessages.find(id);
        require(active!=mBufferedMessages.end(),"submission without pending decision");
        if(active->second.phase==ForwardingPhase::WAITING_PROBABILITY) ++initials;
        else ++fallbacks;
        ++submissions;
        if(!stress()) times.push_back(simTime().dbl());
    }
    void initialize() override {
        artery::CpmService::initialize(); initializeForwarding();
        clock.setTimebase("2018-01-01 00:00:00"); mTimer=&clock;
        test=par("testCase").stdstringValue();
        if(test=="equator") latitude=longitude=0;
        if(test=="dateline") longitude=179.9998;
        vdp.setStationType(vanetza::geonet::StationType::Passenger_Car);
        artery::VehicleKinematics kin;
        kin.geo_position.latitude=latitude*vanetza::units::degree;
        kin.geo_position.longitude=longitude*vanetza::units::degree;
        vdp.update(kin); mVehicleDataProvider=&vdp;
        staticPosition=kin.geo_position;
        if(test=="staticPosition") { mVehicleDataProvider=nullptr; mGeoPosition=&staticPosition; }
        mVdpSnapshot.referenceLatitude=0; mVdpSnapshot.referenceLongitude=0;
        rngStart=getEnvir()->getRNG(0)->getNumbersDrawn();
        action(simTime().dbl()+.1,[this]{ begin(); });
        if(test.substr(0,4)!="live") action(12.1,[this]{ verify(); });
    }
    void begin() {
        if(test=="departure") {
            for(const auto* kind : {"liveProbabilityFinish","liveProbabilityDestroy","liveFallbackFinish","liveFallbackDestroy"}) {
                auto* child=cModuleType::get("WeightedFixture")->create(kind,getParentModule());
                child->par("testCase")=kind;
                child->finalizeParameters(); child->buildInside(); child->scheduleStart(simTime()); child->callInitialize();
                const bool probability=std::string(kind).find("Probability")!=std::string::npos;
                action(probability?.202:.207,[this,child,kind,probability]{
                    auto* service=check_and_cast<WeightedFixture*>(child);
                    require(service->mBufferedMessages.size()==1,"departure without pending state");
                    require(service->mBufferedMessages.begin()->second.phase==(probability?ForwardingPhase::WAITING_PROBABILITY:ForwardingPhase::WAITING_FALLBACK),"wrong departure phase");
                    std::weak_ptr<const artery::Cpm> weak=service->mBufferedMessages.begin()->second.cpm;
                    if(std::string(kind).find("Finish")!=std::string::npos) child->callFinish();
                    child->deleteModule();
                    require(weak.expired(),"departure retained CPM");
                });
            }
            return;
        }
        const int age=test=="expiryInitial"?1998:(test=="expiryExact"?1995:(test=="expiryFallback"?1992:
            (test=="late"?1900:(test=="short"?1970:(test=="expired"?2001:(test=="future"?-100:0))))));
        auto cpm=message(7,age);
        replayBytes=cpm->encode(); released=cpm;
        const bool zero=test=="zero" || test=="cancel" || test=="expiryFallback" || test=="fallbackStress" || test.substr(0,4)=="live";
        const double east=zero?0:((test=="geometry" || test=="minimum")?450:
            ((test=="equalProbability" || test=="randomStress")?250:(test=="dateline"?300:600)));
        if(test=="own") rememberOriginated(*cpm);
        receive(cpm,east); receive(cpm,east); // initial duplicate never adds another decision
        if(test=="own") { require(mBufferedMessages.empty(),"own CPM restarted forwarding"); return; }
        if(test=="expired" || test=="future") { require(mSeenMessages.empty(),"invalid age retained"); return; }
        if(stress()) {
            for(unsigned i=1;i<10000;++i) action(.1+i*.001,[this,i,east]{
                auto next=message(100+i); receive(next,east); receive(next,east); });
            action(10.2,[this]{
                require(peakSeen<=2001,"replay history grows with run length");
                require(peakActive<=12,"payload retained after pending decision");
            });
            return;
        }
        if(test=="identity") {
            auto body=message(7,0,1); receive(body);
            require(identifyMessage(*cpm)!=identifyMessage(*body),"same-time distinct bodies merged");
            auto older=message(7,50,2); receive(older);
            auto origin=message(8); receive(origin);
            require(mBufferedMessages.size()==4,"origin/body/out-of-order identity collision");
            action(.101,[this]{ auto newer=message(); receive(newer);
                require(mBufferedMessages.size()==5,"new CPM at same latitude merged"); });
            artery::Cpm a(*cpm),b(*cpm);
            asn_uint642INTEGER(&a->payload.managementContainer.referenceTime,279);
            asn_uint642INTEGER(&b->payload.managementContainer.referenceTime,3075);
            require(std::get<1>(identifyMessage(a))==279 && std::get<1>(identifyMessage(b))==3075,"reference time decoding");
        }
        if(test=="minimum") {
            action(.102,[this]{ receive(replay(),50);
                require(std::abs(mBufferedMessages.begin()->second.minDij-50)<.01,"minimum must use previous hop and fresh ego"); });
            action(.103,[this]{ receive(replay(),450);
                require(std::abs(mBufferedMessages.begin()->second.minDij-50)<.01,"farther duplicate raised minimum"); });
        }
        if(test=="geometry") {
            require(std::abs(mBufferedMessages.begin()->second.minDij-450)<.01,"last-hop geometry uses stale/origin position");
        }
        if(test=="cancel") action(.107,[this]{
            require(draws==1 && submissions==0,"probability zero must enter fallback");
            receive(replay(),0,true); // even a duplicate with unusable position cancels fallback
            require(mBufferedMessages.empty() && !mSeenMessages.empty(),"fallback not cancelled safely");
            require(released.expired(),"cancelled payload still retained");
        });
        if(test=="cleanup") action(.101,[this]{
            clearForwarding(); clearForwarding();
            require(mBufferedMessages.empty() && mSeenMessages.empty() && mForwardingExpiryTimer==nullptr,"cleanup not idempotent");
            require(released.expired(),"cleanup retained payload");
        });
        if(test=="zero" || test=="equalProbability") action(.106,[this]{
            require(draws==1 && submissions==0 && mBufferedMessages.size()==1,"boundary must fail probability trial"); });
        if(test=="probability" || test=="cancel" || test=="own") {
            action(.5,[this]{ receive(replay()); require(mBufferedMessages.empty(),"completed CPM restarted"); });
            action(2.2,[this]{ receive(replay()); require(mSeenMessages.empty() && mBufferedMessages.empty(),"old replay revived after eviction"); });
        }
        if(test.substr(0,4)!="live") action(.12,[this]{
            require(released.expired(),"full CPM retained after decision completion");
            require(mBufferedMessages.empty(),"decision retained payload/state");
        });
    }
    void verify() {
        // No explicit prune: the idle expiry event must already have run.
        require(mBufferedMessages.empty() && mSeenMessages.empty() && mSeenExpiry.empty(),"idle expiry retained state");
        require(released.expired(),"payload survived validity");
        unsigned expected=1,expectedDraws=1;
        if(test=="identity") expected=expectedDraws=5;
        if(stress()) expected=expectedDraws=10000;
        if(test=="own" || test=="expired" || test=="future" || test=="cleanup" || test=="departure" || test=="expiryInitial" || test=="expiryExact") expected=expectedDraws=0;
        if(test=="cancel" || test=="expiryFallback" || test=="short") expected=0;
        if(submissions!=expected || draws!=expectedDraws)
            throw cRuntimeError("Weighted test %s: submissions %u/%u, draws %u/%u",test.c_str(),submissions,expected,draws,expectedDraws);
        require(mSubmissions==submissions,"common accounting count mismatch");
        const unsigned globalDraws=test=="departure"?2:expectedDraws;
        require(getEnvir()->getRNG(0)->getNumbersDrawn()-rngStart==globalDraws,"unexpected RNG draws");
        if(test=="zero" || test=="equalProbability" || test=="minimum") require(times.size()==1 && std::abs(times[0]-.110)<1e-9,"fallback not additional 5 ms");
        if(test=="probability" || test=="geometry" || test=="dateline" || test=="equator" || test=="staticPosition" || test=="late") require(times.size()==1 && std::abs(times[0]-.105)<1e-9,"initial wait changed");
        if(test=="randomStress") require(initials>4200 && initials<5800 && initials+fallbacks==10000,"interior probability distribution");
        recordScalar("weightedTestPassed",1); recordScalar("testSubmissions",submissions);
        recordScalar("testProbabilityDraws",draws); recordScalar("testInitialSubmissions",initials);
        recordScalar("testFallbackSubmissions",fallbacks); recordScalar("testPeakSeen",peakSeen); recordScalar("testPeakActive",peakActive);
        verified=true;
    }
    void handleMessage(cMessage* message) override {
        if(auto* a=dynamic_cast<Action*>(message)) { auto run=std::move(a->run); delete a; run(); }
        else artery::CpServiceWeightedPPersistence::handleMessage(message);
    }
    void finish() override {
        require(verified || test.substr(0,4)=="live","test did not finish validation");
        artery::CpServiceWeightedPPersistence::finish();
        require(mBufferedMessages.empty() && mSeenMessages.empty() && mForwardingExpiryTimer==nullptr,"finish retained state");
    }
    void writeMetricToCsv(const std::string&,double,long=0) override {}
};
Define_Module(WeightedFixture);

namespace artery {
void ItsG5BaseService::initialize() {
    if(fixture(this)) return;
    static auto call=original<void(*)(ItsG5BaseService*)>("_ZN6artery16ItsG5BaseService10initializeEv"); call(this);
}
void ItsG5BaseService::request(const vanetza::btp::DataRequestB& req,std::unique_ptr<vanetza::DownPacket> packet,const NetworkInterface* interface) {
    if(fixture(this)) { check_and_cast<WeightedFixture*>(this)->sink(req,*packet); return; }
    static auto call=original<void(*)(ItsG5BaseService*,const vanetza::btp::DataRequestB&,std::unique_ptr<vanetza::DownPacket>,const NetworkInterface*)>(
        "_ZN6artery16ItsG5BaseService7requestERKN7vanetza3btp12DataRequestBESt10unique_ptrINS1_11ChunkPacketESt14default_deleteIS7_EEPKNS_16NetworkInterfaceE");
    call(this,req,std::move(packet),interface);
}
}
namespace omnetpp {
double uniform(cRNG* rng,double a,double b) {
    static auto call=original<double(*)(cRNG*,double,double)>("_ZN7omnetpp7uniformEPNS_4cRNGEdd");
    const auto before=rng->getNumbersDrawn();
    const auto actual=call(rng,a,b);
    auto* context=cSimulation::getActiveSimulation()->getContextModule();
    if(auto* service=dynamic_cast<WeightedFixture*>(context)) {
        require(rng->getNumbersDrawn()==before+1,"one probability trial must consume one draw");
        return service->sample(rng,a,b,actual);
    }
    return actual;
}
}
