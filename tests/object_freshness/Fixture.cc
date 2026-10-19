// Controlled production observer tests. Private access is fixture-only; no
// interposition, simulated RNG samples, or protocol changes are used.
#include "artery/application/CpmObjectFreshnessObserver.h"
#include "artery/application/Asn1PacketVisitor.h"
#include "artery/application/CpService.h"
// These pre-existing headers share a guard. Undefine it only in this fixture;
// production header cleanup is outside this measurement change.
#undef ARTERY_CPSERVICE_H_
#include "artery/application/CpServiceStatic.h"
#undef ARTERY_CPSERVICE_H_
#include "artery/application/CpServiceEtsi.h"
#undef ARTERY_CPSERVICE_H_
#include "artery/application/CpServiceLookAhead.h"
#undef ARTERY_CPSERVICE_H_
#include "artery/application/CpServiceVPCast.h"
#undef ARTERY_CPSERVICE_H_
#include "artery/application/CpServiceWeightedPPersistence.h"
#include "artery/application/Middleware.h"
#include "artery/application/VehicleDataProvider.h"
#include "artery/application/NetworkInterface.h"
#include "artery/application/TransportDispatcher.h"
#include "artery/networking/Router.h"
#include "artery/envmod/GlobalEnvironmentModel.h"
#include "artery/envmod/LocalEnvironmentModel.h"
#include <omnetpp.h>
#include <functional>
#include <numeric>
#include <type_traits>

using namespace omnetpp;
using namespace artery;

namespace {
void require(bool value, const char* message) {
    if (!value) throw cRuntimeError("Object freshness test: %s", message);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
class Action : public cMessage {
public:
    explicit Action(std::function<void()> f) : cMessage("freshness fixture action"), run(std::move(f)) {}
    std::function<void()> run;
};
class PhysicalObject : public EnvironmentModelObject {
public:
    std::string id;
    Position pos;
    std::vector<Position> outline;
    PhysicalObject(std::string name, double x) : id(std::move(name)), pos(x, 0) {}
    void update() override {}
    std::string getExternalId() const override { return id; }
    const Position& getCentrePoint() const override { return pos; }
    const std::vector<Position>& getOutline() const override { return outline; }
    const Position& getAttachmentPoint(const SensorPosition&) const override { return pos; }
    Heading getHeading() const override { return Heading{}; }
    Length getLength() const override { return Length{}; }
    Length getWidth() const override { return Length{}; }
    Length getRadius() const override { return Length{}; }
    bool isVisible() override { return true; }
};
}

class FreshnessGlobal : public GlobalEnvironmentModel {
    void initialize() override {}
    void finish() override {}
};
Define_Module(FreshnessGlobal);
class FreshnessLocal : public LocalEnvironmentModel {
    void initialize(int) override {}
    void finish() override { mObjects.clear(); }
    void receiveSignal(cComponent*, simsignal_t, cObject*, cObject*) override {}
};
Define_Module(FreshnessLocal);
class FreshnessMiddleware : public Middleware {
public:
    VehicleDataProvider vdp{42};
    void initialize(int stage) override {
        if (stage) return;
        getFacilities().register_const(&vdp);
        getFacilities().register_mutable(check_and_cast<LocalEnvironmentModel*>(getParentModule()->getSubmodule("environmentModel")));
    }
    void finish() override {}
};
Define_Module(FreshnessMiddleware);

// Skip production initialization/generation only for isolated cold-reception
// instances, while preserving OMNeT++ creation/deletion lifecycle checks.
template<class Base> class SnapshotService : public Base {
    void initialize() override {}
    void finish() override {}
};
class SnapshotCp : public SnapshotService<CpService> {};
class SnapshotStatic : public SnapshotService<CpServiceStatic> {};
class SnapshotEtsi : public SnapshotService<CpServiceEtsi> {};
class SnapshotLookAhead : public SnapshotService<CpServiceLookAhead> {};
class SnapshotVPCast : public SnapshotService<CpServiceVPCast> {};
class SnapshotWeighted : public SnapshotService<CpServiceWeightedPPersistence> {};
Define_Module(SnapshotCp);
Define_Module(SnapshotStatic);
Define_Module(SnapshotEtsi);
Define_Module(SnapshotLookAhead);
Define_Module(SnapshotVPCast);
Define_Module(SnapshotWeighted);
class SnapshotRouter : public Router {
    int numInitStages() const override { return 1; }
    void initialize(int) override {}
    void finish() override {}
};
Define_Module(SnapshotRouter);

class FreshnessFixture : public CpmObjectFreshnessObserver {
    std::shared_ptr<PhysicalObject> x, y;
    std::shared_ptr<const Cpm> held;
    FreshnessGlobal* global = nullptr;
    FreshnessLocal* local = nullptr;
    std::string test;
    int vehicle = 0, rsu = 0;
    std::uint64_t xToken = 0, yToken = 0;
    void at(double t, std::function<void()> f) { scheduleAt(SimTime(t), new Action(std::move(f))); }
    void refresh() { refreshGroundTruth(); }
    void receive(std::shared_ptr<const Cpm> message, int id = 0) {
        CpObject object(message); observeReceived(id ? id : vehicle, object);
    }
    Pair& pair(std::uint64_t token) { return mReceivers.at(vehicle).pairs.at(token); }
    Statistics& stats(unsigned mode = 0, unsigned role = 0) { return mStats[mode][role]; }

    template<class Service>
    std::shared_ptr<const Cpm> encoded(const std::shared_ptr<PhysicalObject>& physical,
        double information, unsigned origin = 1, unsigned objectId = 7, unsigned selected = 1)
    {
        typename Service::VdpSnapshot vdp{};
        vdp.stationId = origin;
        vdp.stationType = static_cast<int>(vanetza::geonet::StationType::RSU);
        auto cpm = [&]() {
            if constexpr (std::is_same<Service, CpServiceLookAhead>::value)
                return createCollectivePerceptionMessageLookAhead(vdp, 0);
            else if constexpr (std::is_same<Service, CpServiceWeightedPPersistence>::value)
                return createCollectivePerceptionMessageWeightedPP(vdp, 0);
            else return createCollectivePerceptionMessage(vdp, 0);
        }();
        if constexpr (std::is_same<Service, CpService>::value) addOriginatingRsuContainer(cpm);
        else if constexpr (std::is_same<Service, CpServiceStatic>::value) addOriginatingRsuContainerStatic(cpm);
        else if constexpr (std::is_same<Service, CpServiceEtsi>::value) addOriginatingRsuContainerEtsi(cpm);
        else if constexpr (std::is_same<Service, CpServiceLookAhead>::value) addOriginatingRsuContainerLookAhead(cpm);
        else if constexpr (std::is_same<Service, CpServiceVPCast>::value) addOriginatingRsuContainerVPCast(cpm);
        else addOriginatingRsuContainerWeightedPP(cpm);
        std::vector<typename Service::PerceivedObjectSnapshot> snapshots(selected);
        std::vector<std::size_t> indices(selected);
        std::iota(indices.begin(), indices.end(), 0);
        for (unsigned i = 0; i < selected; ++i) {
            auto& snap = snapshots[i];
            snap.cpsId = objectId + i; snap.hasVelocity = true; snap.sensorIds = {0};
            snap.objectPerceptionQuality = 3;
            snap.provenance = CpmObjectProvenance{physical, SimTime(information), bool(physical)};
        }
        if constexpr (std::is_same<Service, CpServiceLookAhead>::value)
            addPerceivedObjectContainerLookAhead(cpm, snapshots, indices, SimTime::ZERO);
        else if constexpr (std::is_same<Service, CpServiceWeightedPPersistence>::value)
            addPerceivedObjectContainerWeightedPP(cpm, snapshots, indices, SimTime::ZERO);
        else addPerceivedObjectContainer(cpm, snapshots, indices, SimTime::ZERO);
        const auto before = cpm.encode();
        auto message = std::make_shared<const Cpm>(std::move(cpm));
        attachCpmObjectProvenance(message, snapshots, indices);
        require(message->encode() == before, "metadata changed transmitted bytes");
        return message;
    }
    std::shared_ptr<const Cpm> message(const std::shared_ptr<PhysicalObject>& object,
        double information, unsigned origin = 1, unsigned objectId = 7) {
        return encoded<CpService>(object, information, origin, objectId);
    }
    // Exercise production reception before any generation/initialize refresh.
    // Forwarding is disabled only in these cold-start test instances.
    template<class Service>
    void snapshotCase(const char* type, bool rsuSnapshot) {
        auto* middleware = check_and_cast<Middleware*>(getParentModule()->getSubmodule("host",0)->getSubmodule("middleware"));
        auto* service = check_and_cast<Service*>(cModuleType::get(type)->createScheduleInit("snapshotTest",middleware));
        require(!service->mVdpSnapshotValid, "new snapshot unexpectedly valid");
        const auto& initial = service->mVdpSnapshot;
        require(initial.updated == SimTime::ZERO && initial.stationId == 0 &&
            initial.stationType == static_cast<int>(vanetza::geonet::StationType::Unknown) &&
            near(initial.position.x.value(),0) && near(initial.position.y.value(),0) &&
            initial.referenceLatitude == 900000001 && initial.referenceLongitude == 1800000001 &&
            initial.orientationAngleDeciDeg == 36001, "uninitialized snapshot field");
        service->m_middleware = middleware;
        VehicleDataProvider provider{42};
        provider.setStationType(vanetza::geonet::StationType::Passenger_Car);
        VehicleKinematics kin;
        kin.position = Position(12,34);
        kin.geo_position.latitude = 0 * vanetza::units::degree;
        kin.geo_position.longitude = 0 * vanetza::units::degree;
        provider.update(kin);
        service->mVehicleDataProvider = rsuSnapshot ? nullptr : &provider;
        static GeoPosition geo;
        geo.latitude = 0 * vanetza::units::degree;
        geo.longitude = 0 * vanetza::units::degree;
        static Identity identity;
        identity.application = 777;
        middleware->getFacilities().register_const(&geo);
        middleware->getFacilities().register_const(&identity);
        service->scSignalCpmReceivedDistance = registerSignal("cpmReceivedDistance");
        service->scSignalCpmExpectedDistance = registerSignal("cpmExpectedDistance");
        if constexpr (std::is_same<Service,CpService>::value) service->mRedundancySuppressionMode = 1;
        if constexpr (std::is_same<Service,CpServiceVPCast>::value ||
                      std::is_same<Service,CpServiceWeightedPPersistence>::value) service->mForwardingFinished = true;
        typename CpService::VdpSnapshot source{};
        source.stationId = 999;
        source.referenceLatitude = source.referenceLongitude = 0;
        auto cpm = createCollectivePerceptionMessage(source,0);
        addOriginatingRsuContainer(cpm);
        require(cpm.validate(), "invalid cold reception fixture");
        auto shared = std::make_shared<const Cpm>(std::move(cpm));
        struct NoDcc : IDccEntity {
            void reportLocalChannelLoad(vanetza::dcc::ChannelLoad) override {}
            vanetza::dcc::ChannelProbeProcessor* getChannelProbeProcessor() override { return nullptr; }
            vanetza::dcc::RequestInterface* getRequestInterface() override { return nullptr; }
            vanetza::dcc::TransmitRateThrottle* getTransmitRateThrottle() override { return nullptr; }
            vanetza::geonet::DccFieldGenerator* getGeonetFieldGenerator() override { return nullptr; }
        } dcc;
        TransportDispatcher dispatcher;
        auto* router = check_and_cast<Router*>(cModuleType::get("SnapshotRouter")->createScheduleInit("snapshotRouter",middleware));
        NetworkInterface network(*router,dcc,dispatcher);
        auto receive = [&] {
            vanetza::ChunkPacket chunks;
            chunks.layer(vanetza::OsiLayer::Application) = std::unique_ptr<vanetza::convertible::byte_buffer>(
                new vanetza::convertible::byte_buffer_impl<Cpm>(shared));
            auto packet = std::make_unique<vanetza::UpPacket>(std::move(chunks));
            vanetza::btp::DataIndication indication;
            if constexpr (std::is_same<Service,CpServiceVPCast>::value)
                service->indicate(indication,std::move(packet),network);
            else service->indicate(indication,std::move(packet));
        };
        receive();
        require(service->mVdpSnapshotValid && service->mVdpSnapshot.referenceLatitude == 0 &&
            service->mVdpSnapshot.referenceLongitude == 0, "cold reception did not capture valid zero coordinates");
        require(service->mVdpSnapshot.stationId == (rsuSnapshot ? 777u : 42u), "wrong snapshot station");
        require(service->mVdpSnapshot.stationType == static_cast<int>(rsuSnapshot ?
            vanetza::geonet::StationType::RSU : vanetza::geonet::StationType::Passenger_Car), "wrong snapshot role");
        if (!rsuSnapshot) require(near(service->mVdpSnapshot.position.x.value(),12), "vehicle position not captured");
        const auto cached = service->mVdpSnapshot;
        kin.position = Position(99,98);
        kin.geo_position.latitude = 48 * vanetza::units::degree;
        // Two fixture updates share an event; VDP normally updates once per time step.
        provider.mLastUpdate = simTime() - SimTime(.01);
        provider.update(kin);
        geo.latitude = 48 * vanetza::units::degree;
        receive();
        require(service->mVdpSnapshot.referenceLatitude == 0 &&
            service->mVdpSnapshot.updated == cached.updated &&
            service->mVdpSnapshot.position == cached.position, "reception refreshed a valid zero-coordinate cache");
        service->captureVdpSnapshot();
        require(service->mVdpSnapshot.referenceLatitude == 480000000, "normal explicit snapshot refresh stopped working");
        service->deleteModule();
        router->deleteModule();
    }
    void localDetection() {
        auto found = local->mObjects.find(LocalEnvironmentModel::Object(x));
        if (found == local->mObjects.end()) local->mObjects.emplace(x, LocalEnvironmentModel::Tracking{1, nullptr});
        else found->second.tap(nullptr);
        // Exercise the production deferred local-sensing path, after the refresh signal.
        global->emit(cComponent::registerSignal("EnvironmentModel.refresh"), global);
    }

    void setup() {
        global = check_and_cast<FreshnessGlobal*>(getParentModule()->getSubmodule("environmentModel"));
        mEnvironment = global;
        auto* vehicleHost = getParentModule()->getSubmodule("host", 0);
        auto* rsuHost = getParentModule()->getSubmodule("host", 1);
        vehicle = vehicleHost->getId(); rsu = rsuHost->getId();
        for (auto* host : {vehicleHost, rsuHost}) {
            Receiver r; r.rsu = host == rsuHost;
            r.ego = r.rsu ? "rsu" : "ego";
            r.middleware = check_and_cast<Middleware*>(host->getSubmodule("middleware"));
            mReceivers.emplace(host->getId(), std::move(r));
        }
        local = check_and_cast<FreshnessLocal*>(vehicleHost->getSubmodule("environmentModel"));
        x = std::make_shared<PhysicalObject>("physical-X", 100);
        y = std::make_shared<PhysicalObject>("physical-Y", 200);
        global->mObjects.emplace(x->id, x); global->mObjects.emplace(y->id, y);
        refresh();
        xToken = mObjects.at(Object(x)).token; yToken = mObjects.at(Object(y)).token;
        require(mReceivers.at(vehicle).pairs.size() == 2, "zero-update eligible objects were not registered");
        const auto baseDraws = getRNG(0)->getNumbersDrawn();

        if (test == "identity") {
            at(.1, [this] { receive(message(x,.05,1,7)); });
            at(.2, [this] { receive(message(y,.15,2,7)); require(stats().intervals == 0, "cross-station ID collision merged physical objects"); });
            at(.3, [this] { receive(message(x,.25,3,8)); require(stats().intervals == 1 && near(stats().intervalSum,.2), "reporter/ID changes split one physical object"); });
        } else if (test == "multiReporter") {
            at(.1, [this] { receive(message(x,.05,1,7)); });
            at(.2, [this] { receive(message(x,.15,2,99)); });
            at(.3, [this] { receive(message(x,.10,3,12)); });
            at(.4, [this] { receive(message(x,.15,4,40)); });
            at(.5, [this] { receive(message(x,.45,5,7)); require(stats().updates == 3 && stats().intervals == 2 && near(stats().intervalSum,.4), "multi-source freshness watermark failed"); });
        } else if (test == "duplicate") {
            at(.1, [this] { held = message(x,.05); receive(held); });
            at(.2, [this] {
                vanetza::ChunkPacket original;
                original.layer(vanetza::OsiLayer::Application) = std::unique_ptr<vanetza::convertible::byte_buffer>(
                    new vanetza::convertible::byte_buffer_impl<Cpm>(held));
                auto forwarded = original; auto secondRelay = forwarded;
                Asn1PacketVisitor<Cpm> visitor; visitor(secondRelay);
                require(visitor.shared_wrapper == held, "relay packet copies lost provenance owner");
                receive(visitor.shared_wrapper); receive(held);
                require(stats().updates == 1 && stats().intervals == 0, "unchanged relays counted as updates");
            });
            at(.5, [this] { receive(message(x,.45)); require(near(stats().intervalSum,.4), "duplicates reset arrival clock"); held.reset(); });
        } else if (test == "outOfOrder") {
            at(.1, [this] { receive(message(x,.09)); });
            at(.2, [this] { receive(message(x,.04)); });
            at(.3, [this] { receive(message(y,.03)); require(pair(yToken).streams[0].updated, "old CPM time hid an unseen physical object"); });
            at(.4, [this] { receive(message(x,.35)); require(near(stats().intervalSum,.3), "out-of-order data reset freshness clock"); });
        } else if (test == "local") {
            at(.1, [this] { localDetection(); });
            at(.11, [this] { require(stats().updates == 0 && stats(1).updates == 1, "local sensing polluted CPM-only metric"); });
            at(.2, [this] { receive(message(x,.05)); });
            at(.3, [this] { localDetection(); });
            at(.4, [this] { receive(message(x,.35)); });
            at(.5, [this] { observeLocal(); require(stats().updates == 2 && stats(1).updates == 3 && near(stats().intervalSum,.2) && near(stats(1).intervalSum,.3), "combined sensing timestamps or stale-track filtering failed"); });
        } else if (test == "range") {
            at(.1, [this] { receive(message(x,.05)); });
            at(.2, [this] { x->pos = Position(501,0); refresh(); require(!pair(xToken).eligible, "object beyond 500 m remained eligible"); });
            at(.3, [this] { receive(message(x,.25)); });
            at(.4, [this] { x->pos = Position(500,0); refresh(); require(pair(xToken).eligible, "500 m boundary excluded"); });
            at(.5, [this] { receive(message(x,.25)); require(!pair(xToken).streams[0].updated, "range re-entry refreshed old information"); });
            at(.6, [this] { receive(message(x,.55)); require(stats().intervals == 0 && stats().endings[RangeExit] == 1, "range exit did not censor/restart the interval episode"); });
        } else if (test == "lifecycle") {
            at(.1, [this] { held = message(x,.05); receive(held); });
            at(.2, [this] { global->mObjects.erase(x->id); refresh(); require(!mReceivers.at(vehicle).pairs.count(xToken), "object departure retained pair"); });
            at(.3, [this] { receive(held); require(mInactiveObjects == 1, "old object lifetime reactivated"); x = std::make_shared<PhysicalObject>("physical-X",100); global->mObjects[x->id] = x; refresh(); require(mObjects.at(Object(x)).token != xToken, "reused external ID merged lifetimes"); xToken = mObjects.at(Object(x)).token; });
            at(.4, [this] { receive(message(x,.35)); require(stats().intervals == 0, "new physical lifetime bridged departure"); });
            at(.5, [this] { removeReceiver(vehicle); require(!mReceivers.count(vehicle), "receiver departure retained pair state"); require(stats().endings[ReceiverDeparture] == 2, "receiver departure gaps were not censored"); held.reset(); });
        } else if (test == "missing") {
            at(.1, [this] { receive(message(nullptr,.05)); require(mUnresolved == 1 && stats().updates == 0, "missing provenance invented physical object zero"); });
            at(.2, [this] { receive(message(x,.15,1,0)); require(stats().updates == 1, "valid object ID zero rejected"); });
            at(.3, [this] { auto source = message(x,.25); auto decoded = std::make_shared<Cpm>(); require(decoded->decode(source->encode()), "decode fixture failed"); receive(decoded); require(mMissingMetadata == 1 && stats().updates == 1, "unresolved raw decode guessed physical identity"); });
            at(.4, [this] { receive(message(x,.9)); require(mInvalidTimes == 2 && stats().updates == 1, "future source timestamp accepted"); });
        } else if (test == "repack") {
            at(.1, [this] { held = message(x,.05); receive(held); });
            at(.2, [this] {
                const auto& containers = (*held)->payload.cpmContainers.list;
                const Vanetza_ITS2_PerceivedObject_t* entry = nullptr;
                for (int i=0; i<containers.count; ++i)
                    if (containers.array[i]->containerId == Vanetza_ITS2_CpmContainerId_perceivedObjectContainer)
                        entry = containers.array[i]->containerData.choice.PerceivedObjectContainer.perceivedObjects.list.array[0];
                const auto cached = lookupCpmObjectProvenance(held,entry);
                require(cached.known && cached.informationTime == SimTime(.05), "RSU cache lost original observation time");
                auto repacked = message(x,.19,99,44);
                publishCpmObjectProvenance(repacked,{cached});
                held.reset(); pruneMetadata();
                receive(repacked); receive(repacked,rsu);
                require(stats().updates == 1 && stats(0,1).updates == 1, "repacking refreshed cached information or lost another receiver's first observation");
                require(mMetadata.at(Message(repacked))[0].informationTime == SimTime(.05), "re-originating reset metadata timestamp");
            });
        } else if (test == "encoders") {
            at(.1, [this] {
                const auto check = [this](auto message) { require(mMetadata.at(Message(message)).size() == 255, "metadata ignored encoded 255-object limit"); receive(message); };
                check(encoded<CpService>(x,.01,1,0,260));
                check(encoded<CpServiceStatic>(x,.02,2,0,260));
                check(encoded<CpServiceEtsi>(x,.03,3,0,260));
                check(encoded<CpServiceLookAhead>(x,.04,4,0,260));
                check(encoded<CpServiceVPCast>(x,.05,5,0,260));
                check(encoded<CpServiceWeightedPPersistence>(x,.06,6,0,260));
                require(stats().updates == 1 && stats().intervals == 0 && stats().coalesced == 5, "same-time reports were not coalesced");
            });
        } else if (test == "warmup") {
            at(.1, [this] { receive(message(x,.05)); });
            at(.3, [this] { receive(message(x,.05)); require(stats().updates == 0, "pre-warm-up knowledge became fresh again"); });
            at(.4, [this] { receive(message(x,.35)); require(stats().intervals == 0 && near(stats().initialWaitSum,.15), "warm-up boundary interval was bridged"); });
            at(.5, [this] { receive(message(x,.45)); require(near(stats().intervalSum,.1), "post-warm-up interval incorrect"); });
        } else if (test == "idle") {
            at(.1, [this] { receive(message(x,.05)); });
            at(100.1, [this] { receive(message(x,100.05)); require(stats().intervals == 1 && near(stats().intervalSum,100), "long silence was truncated or forgotten"); require(mReceivers.at(vehicle).pairs.size() == 2, "idle duration grew pair history"); });
        } else if (test == "stress") {
            for (unsigned i = 0; i < 10000; ++i) {
                const double t = .02 + i*.001;
                at(t, [this, t] { auto value = message(x,t-.001); receive(value); receive(value); });
            }
            at(11.2, [this] { require(stats().updates == 10000 && stats().intervals == 9999, "sustained traffic counted duplicates"); require(mMetadata.empty() && mMetadataEntries == 0, "idle pruning retained expired payload metadata"); require(mPeakMetadata <= 1001 && mPeakPairs == 4, "state scaled with total message history"); });
        } else if (test == "roles") {
            at(.1, [this] { auto value = message(x,.05); receive(value); receive(value,rsu); });
            at(.2, [this] { auto value = message(x,.15); receive(value); receive(value,rsu); require(stats().intervals == 1 && stats(0,1).intervals == 1, "vehicle and RSU populations were not separate"); });
        } else if (test == "snapshots") {
            at(.1, [this] {
                for (bool rsuSnapshot : {false,true}) {
                    snapshotCase<CpService>("SnapshotCp",rsuSnapshot);
                    snapshotCase<CpServiceStatic>("SnapshotStatic",rsuSnapshot);
                    snapshotCase<CpServiceEtsi>("SnapshotEtsi",rsuSnapshot);
                    snapshotCase<CpServiceLookAhead>("SnapshotLookAhead",rsuSnapshot);
                    snapshotCase<CpServiceVPCast>("SnapshotVPCast",rsuSnapshot);
                    snapshotCase<CpServiceWeightedPPersistence>("SnapshotWeighted",rsuSnapshot);
                }
            });
        } else if (test == "receiveHook") {
            at(.1, [this] { CpObject value(message(x,.05)); observeCpmObjectFreshness(mReceivers.at(vehicle).middleware,value); });
            at(.2, [this] { CpObject value(message(x,.15)); observeCpmObjectFreshness(mReceivers.at(vehicle).middleware,value); require(stats().updates==2 && stats().intervals==1, "common valid-CPM entry hook missed vehicle receptions"); });
        } else if (test == "boundary") {
            at(.1, [this] { auto& p=pair(xToken); closePair(mReceivers.at(vehicle),p,RangeExit); beginPair(p); receive(message(x,.05)); closePair(mReceivers.at(vehicle),p,RangeExit); require(stats().zeroExposure==1 && stats().episodes==stats().initialCompleted+stats().initialCensored, "instantaneous episode broke censor accounting"); });
        } else if (test != "termination") throw cRuntimeError("Unknown object freshness test");

        const double checkAt = test == "idle" ? 100.8 : test == "stress" ? 11.5 : .8;
        at(checkAt, [this,baseDraws] {
            require(getRNG(0)->getNumbersDrawn() == baseDraws, "observer consumed simulation random numbers");
            if (test == "termination") { require(stats().updates == 0, "zero-traffic case invented an update"); }
        });
    }
    void initialize() override {
        CpmObjectFreshnessObserver::initialize();
        test = par("testCase").stdstringValue();
        at(.01, [this] { setup(); });
    }
    void handleMessage(cMessage* message) override {
        if (auto* action = dynamic_cast<Action*>(message)) { auto run = std::move(action->run); delete action; run(); }
        else CpmObjectFreshnessObserver::handleMessage(message);
    }
    void finish() override {
        CpmObjectFreshnessObserver::finish();
        require(mReceivers.empty() && mObjects.empty() && mMetadata.empty(), "finish retained observer state");
        if (test == "termination") require(stats().episodes == 2 && stats().zeroUpdates == 2 && stats().initialCensored == 2, "zero-update episodes missing from termination accounting");
        if (test == "idle") require(stats().zeroUpdates == 1 && stats().gapCensored == 1, "long-quiet and never-updated objects were not distinguished");
        recordScalar("objectFreshnessTestPassed",1);
    }
};
Define_Module(FreshnessFixture);
