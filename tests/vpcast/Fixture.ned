simple VpcFixtureRouter { parameters: @class(VpcFixtureRouter); }
simple VpcFixture extends artery.application.CpServiceVPCast
{
    parameters:
        @class(VpcFixture);
        string testCase;
}
network VpcFixtureNetwork
{
    submodules:
        router: VpcFixtureRouter;
        service: VpcFixture;
}
