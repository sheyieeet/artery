simple WeightedFixture extends artery.application.CpServiceWeightedPPersistence
{
    parameters:
        @class(WeightedFixture);
        string testCase;
}
network WeightedFixtureNetwork
{
    submodules:
        service: WeightedFixture;
}
