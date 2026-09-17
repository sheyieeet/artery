//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package artery.application;

simple CpServiceStatic like ItsG5Service
{
    parameters:
        @signal[CpmReceived](type=CpObject);
        @signal[CpmSent](type=CpObject);
        @signal[CpmObjectUpdateInterval](type=double);
        @signal[CpmRxThroughput](type=double);
        @signal[CpmRxObjectCount](type=long);
        @signal[CpmTxThroughput](type=double);
        @signal[CpmTxObjectCount](type=long);
        @signal[CpmChannelLoad](type=double);
        @signal[cpmPrr](type=double);

        @statistic[reception](source="count(CpmReceived)";record=vector);
        @statistic[transmission](source="count(CpmSent)";record=vector);
        @statistic[cpmObjectUpdateInterval](source=CpmObjectUpdateInterval; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmRxThroughput](source=CpmRxThroughput; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmRxObjectCount](source=CpmRxObjectCount; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmTxThroughput](source=CpmTxThroughput; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmTxObjectCount](source=CpmTxObjectCount; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmChannelLoad](source=CpmChannelLoad; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmPrr](title="Packet Receive Ratio"; source="cpmPrr"; record=vector,mean; interpolationmode=sample-hold);

        // evaluate DCC transmission interval restrictions
        bool withDccRestriction = default(true);

        // generation interval boundaries
        double minInterval @unit(s) = default(0.1s);
        double maxInterval @unit(s) = default(1.0s);
        
        double addSensorInformation @unit(s) = default(1.0s);
        int objectInclusionConfig @enum(0,1) = default(1);
        double objectPerceptionQualityThreshold = default(3.0);
        double minPositionChangeThreshold @unit(m) = default(4.0m);
        double minGroundSpeedChangeThreshold @unit(mps) = default(0.5mps);
        double minGroundVelocityOrientationChangeThreshold @unit(deg) = default(4.0deg);

        double minPositionChangePriorityThreshold @unit(m) = default(0.0m);
        double maxPositionChangePriorityThreshold @unit(m) = default(8.0m);
        double minGroundSpeedChangePriorityThreshold @unit(mps) = default(0.0mps);
        double maxGroundSpeedChangePriorityThreshold @unit(mps) = default(1.0mps);
        double minGroundVelocityOrientationChangePriorityThreshold @unit(deg) = default(0.0deg);
        double maxGroundVelocityOrientationChangePriorityThreshold @unit(deg) = default(8.0deg);
        double minLastInclusionTimePriorityThreshold @unit(s) = default(0.1s);
        double maxLastInclusionTimePriorityThreshold @unit(s) = default(1.0s);
        double unusedObjectIdRetentionPeriod @unit(s) = default(60s);
        double unusedSensorIdRetentionPeriod @unit(s) = default(60s);

        // redundancy mitigation parameters
        int redundancySuppressionMode @enum(0,1,2) = default(1); // 0: disabled, 1: all, 2: any
        double redundancyDistanceThreshold @unit(m) = default(4.0m);
        double redundancyHistoryLifetime @unit(s) = default(1.0s);

        // perception rate
        double perceptionTimeWindow @unit(s) = default(1.0s);
        double perceptionRateInterval @unit(s) = default(1.0s);
        @signal[cpmPerceptionRate25](type=double);
        @statistic[cpmPerceptionRate25](source=cpmPerceptionRate25; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate50](type=double);
        @statistic[cpmPerceptionRate50](source=cpmPerceptionRate50; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate75](type=double);
        @statistic[cpmPerceptionRate75](source=cpmPerceptionRate75; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate100](type=double);
        @statistic[cpmPerceptionRate100](source=cpmPerceptionRate100; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate125](type=double);
        @statistic[cpmPerceptionRate125](source=cpmPerceptionRate125; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate150](type=double);
        @statistic[cpmPerceptionRate150](source=cpmPerceptionRate150; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate175](type=double);
        @statistic[cpmPerceptionRate175](source=cpmPerceptionRate175; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate200](type=double);
        @statistic[cpmPerceptionRate200](source=cpmPerceptionRate200; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate225](type=double);
        @statistic[cpmPerceptionRate225](source=cpmPerceptionRate225; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate250](type=double);
        @statistic[cpmPerceptionRate250](source=cpmPerceptionRate250; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate275](type=double);
        @statistic[cpmPerceptionRate275](source=cpmPerceptionRate275; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate300](type=double);
        @statistic[cpmPerceptionRate300](source=cpmPerceptionRate300; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate325](type=double);
        @statistic[cpmPerceptionRate325](source=cpmPerceptionRate325; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate350](type=double);
        @statistic[cpmPerceptionRate350](source=cpmPerceptionRate350; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate375](type=double);
        @statistic[cpmPerceptionRate375](source=cpmPerceptionRate375; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate400](type=double);
        @statistic[cpmPerceptionRate400](source=cpmPerceptionRate400; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate425](type=double);
        @statistic[cpmPerceptionRate425](source=cpmPerceptionRate425; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate450](type=double);
        @statistic[cpmPerceptionRate450](source=cpmPerceptionRate450; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate475](type=double);
        @statistic[cpmPerceptionRate475](source=cpmPerceptionRate475; record=vector,mean; interpolationmode=none);
        @signal[cpmPerceptionRate500](type=double);
        @statistic[cpmPerceptionRate500](source=cpmPerceptionRate500; record=vector,mean; interpolationmode=none);
}
