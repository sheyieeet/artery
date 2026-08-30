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

simple CpService like ItsG5Service
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

        @statistic[reception](source="count(CpmReceived)";record=vector);
        @statistic[transmission](source="count(CpmSent)";record=vector);
        @statistic[cpmObjectUpdateInterval](source=CpmObjectUpdateInterval; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmRxThroughput](source=CpmRxThroughput; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmRxObjectCount](source=CpmRxObjectCount; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmTxThroughput](source=CpmTxThroughput; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmTxObjectCount](source=CpmTxObjectCount; record=vector,mean,max,min; interpolationmode=none);
        @statistic[cpmChannelLoad](source=CpmChannelLoad; record=vector,mean,max,min; interpolationmode=none);

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
}
