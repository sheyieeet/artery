simple CpmAccountingFixture extends artery.application.CpService
{
    parameters:
        @class(CpmAccountingFixture);
        string kind;
}
simple CpmAccountingCoordinator {}
network CpmAccountingNetwork
{
    submodules:
        coordinator: CpmAccountingCoordinator;
}
