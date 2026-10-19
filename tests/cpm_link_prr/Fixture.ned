simple PrrFixtureService extends artery.application.CpService
{
    parameters:
        @class(PrrFixtureService);
}
simple PrrFixtureMiddleware extends artery.application.Middleware
{
    parameters:
        @class(PrrFixtureMiddleware);
        datetime = "2018-03-19 10:00:00";
        services = xml("<services/>");
}
simple PrrFixtureDriver
{
    gates:
        input in;
        output out;
}
simple PrrFixtureControl {}

module PrrFixtureNode
{
    parameters:
        @networkNode;
        bool rsu = default(false);
        bool equipped = default(true);
        double x @unit(m) = default(0m);
        *.interfaceTableModule = default(absPath(".interfaceTable"));
    gates:
        input radioIn @directIn;
    submodules:
        interfaceTable: inet.networklayer.common.InterfaceTable;
        mobility: inet.mobility.static.StationaryMobility {
            parameters:
                initialX = default(x);
        }
        wlan: artery.inet.VanetNic {
            parameters:
                radio.antenna.mobilityModule = ".mobility";
        }
        middleware: PrrFixtureMiddleware;
        driver: PrrFixtureDriver;
    connections:
        radioIn --> wlan.radioIn;
        driver.out --> wlan.upperLayerIn;
        driver.in <-- wlan.upperLayerOut;
}

network PrrFixtureNetwork
{
    parameters:
        string testCase = default("lossfree");
        int stations = default(4);
        bool withObserver = default(true);
    submodules:
        cpmLinkPrr: artery.inet.CpmLinkPrrObserver if withObserver;
        radioMedium: inet.physicallayer.ieee80211.packetlevel.Ieee80211ScalarRadioMedium;
        node[stations]: PrrFixtureNode {
            parameters:
                x = index * 25m;
        }
        control: PrrFixtureControl;
}
