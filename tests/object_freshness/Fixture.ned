simple FreshnessFixture extends artery.application.CpmObjectFreshnessObserver
{
    parameters:
        @class(FreshnessFixture);
        string testCase;
}
simple FreshnessGlobal extends artery.envmod.GlobalEnvironmentModel
{
    parameters:
        @class(FreshnessGlobal);
        traciModule = "";
        nodeMobilityModule = "";
        identityRegistryModule = "";
}
simple FreshnessLocal extends artery.envmod.LocalEnvironmentModel
{
    parameters:
        @class(FreshnessLocal);
        globalEnvironmentModule = "";
        middlewareModule = "";
        sensors = xml("<sensors/>");
}
simple FreshnessMiddleware extends artery.application.Middleware
{
    parameters:
        @class(FreshnessMiddleware);
        datetime = "2018-03-19 10:00:00";
        services = xml("<services/>");
}
module FreshnessHost
{
    submodules:
        environmentModel: FreshnessLocal;
        middleware: FreshnessMiddleware;
}
network FreshnessNetwork
{
    submodules:
        observer: FreshnessFixture;
        environmentModel: FreshnessGlobal;
        host[2]: FreshnessHost;
}

simple SnapshotCp extends artery.application.CpService
{
    parameters:
        @class(SnapshotCp);
}

simple SnapshotStatic extends artery.application.CpServiceStatic
{
    parameters:
        @class(SnapshotStatic);
}

simple SnapshotEtsi extends artery.application.CpServiceEtsi
{
    parameters:
        @class(SnapshotEtsi);
}

simple SnapshotLookAhead extends artery.application.CpServiceLookAhead
{
    parameters:
        @class(SnapshotLookAhead);
}

simple SnapshotVPCast extends artery.application.CpServiceVPCast
{
    parameters:
        @class(SnapshotVPCast);
}

simple SnapshotWeighted extends artery.application.CpServiceWeightedPPersistence
{
    parameters:
        @class(SnapshotWeighted);
}

simple SnapshotRouter extends artery.networking.Router
{
    parameters:
        @class(SnapshotRouter);
        dccModule = "";
        middlewareModule = "";
        radioDriverModule = "";
        runtimeModule = "";
        securityModule = "";
        deferInitialBeacon = 0s;
}
