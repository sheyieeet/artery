package artery.application;

// Physical-object freshness: object-to-receiver eligibility, independent of link PRR.
simple CpmObjectFreshnessObserver
{
    parameters:
        bool enabled = default(true);
        double eligibilityRange @unit(m) = default(500m);
        bool withLocalSensing = default(true);
        @display("i=block/table");
        @signal[CpmPhysicalObjectFreshUpdateIntervalVehicle](type=double);
        @statistic[cpmPhysicalObjectFreshUpdateIntervalVehicle](source=CpmPhysicalObjectFreshUpdateIntervalVehicle; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectInitialWaitVehicle](type=double);
        @statistic[cpmPhysicalObjectInitialWaitVehicle](source=CpmPhysicalObjectInitialWaitVehicle; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectCensoredInitialWaitVehicle](type=double);
        @statistic[cpmPhysicalObjectCensoredInitialWaitVehicle](source=CpmPhysicalObjectCensoredInitialWaitVehicle; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectCensoredGapVehicle](type=double);
        @statistic[cpmPhysicalObjectCensoredGapVehicle](source=CpmPhysicalObjectCensoredGapVehicle; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectFreshUpdateIntervalRsu](type=double);
        @statistic[cpmPhysicalObjectFreshUpdateIntervalRsu](source=CpmPhysicalObjectFreshUpdateIntervalRsu; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectInitialWaitRsu](type=double);
        @statistic[cpmPhysicalObjectInitialWaitRsu](source=CpmPhysicalObjectInitialWaitRsu; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectCensoredInitialWaitRsu](type=double);
        @statistic[cpmPhysicalObjectCensoredInitialWaitRsu](source=CpmPhysicalObjectCensoredInitialWaitRsu; record=vector; interpolationmode=none);
        @signal[CpmPhysicalObjectCensoredGapRsu](type=double);
        @statistic[cpmPhysicalObjectCensoredGapRsu](source=CpmPhysicalObjectCensoredGapRsu; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectFreshUpdateIntervalVehicle](type=double);
        @statistic[combinedPhysicalObjectFreshUpdateIntervalVehicle](source=CombinedPhysicalObjectFreshUpdateIntervalVehicle; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectInitialWaitVehicle](type=double);
        @statistic[combinedPhysicalObjectInitialWaitVehicle](source=CombinedPhysicalObjectInitialWaitVehicle; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectCensoredInitialWaitVehicle](type=double);
        @statistic[combinedPhysicalObjectCensoredInitialWaitVehicle](source=CombinedPhysicalObjectCensoredInitialWaitVehicle; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectCensoredGapVehicle](type=double);
        @statistic[combinedPhysicalObjectCensoredGapVehicle](source=CombinedPhysicalObjectCensoredGapVehicle; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectFreshUpdateIntervalRsu](type=double);
        @statistic[combinedPhysicalObjectFreshUpdateIntervalRsu](source=CombinedPhysicalObjectFreshUpdateIntervalRsu; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectInitialWaitRsu](type=double);
        @statistic[combinedPhysicalObjectInitialWaitRsu](source=CombinedPhysicalObjectInitialWaitRsu; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectCensoredInitialWaitRsu](type=double);
        @statistic[combinedPhysicalObjectCensoredInitialWaitRsu](source=CombinedPhysicalObjectCensoredInitialWaitRsu; record=vector; interpolationmode=none);
        @signal[CombinedPhysicalObjectCensoredGapRsu](type=double);
        @statistic[combinedPhysicalObjectCensoredGapRsu](source=CombinedPhysicalObjectCensoredGapRsu; record=vector; interpolationmode=none);
}
