#!/bin/bash

# Ensure we are in the directory where omnetpp.ini lives
cd "$(dirname "$0")"

# Create the results directory
mkdir -p results

# The raw OMNeT++ execution command
OPP_CMD="/home/dreamwalker/omnetpp-5.6.2/bin/opp_run_release -n /home/dreamwalker/artery/src/artery:/home/dreamwalker/artery/src/traci:/home/dreamwalker/artery/extern/veins/examples/veins:/home/dreamwalker/artery/extern/veins/src/veins:/home/dreamwalker/artery/extern/inet/src:/home/dreamwalker/artery/extern/inet/examples:/home/dreamwalker/artery/extern/inet/tutorials:/home/dreamwalker/artery/extern/inet/showcases:/home/dreamwalker/artery/src/artery/storyboard:/home/dreamwalker/artery/src/artery/envmod:/home/dreamwalker/artery/scenarios/highway-police -l /home/dreamwalker/artery/build/src/artery/envmod/libartery_envmod.so -l /home/dreamwalker/artery/build/scenarios/highway-police/libartery_police.so -l /home/dreamwalker/artery/build/src/artery/envmod/libartery_envmod.so -l /home/dreamwalker/artery/build/src/artery/storyboard/libartery_storyboard.so -l /home/dreamwalker/artery/build/extern/libINET.so -l /home/dreamwalker/artery/build/extern/libveins.so -l /home/dreamwalker/artery/build/src/traci/libtraci.so -l /home/dreamwalker/artery/build/libartery_core.so omnetpp.ini"

echo "Starting Batch 1 (Dynamic & Static)..."
$OPP_CMD -c Proposed > results/log_dynamic.txt 2>&1 &
$OPP_CMD -c Static > results/log_static.txt 2>&1 &
wait 

echo "Starting Batch 2 (ETSI & LookAhead)..."
$OPP_CMD -c ETSI > results/log_etsi.txt 2>&1 &
$OPP_CMD -c LookAhead > results/log_lookAhead.txt 2>&1 &
wait

echo "Starting Batch 3 (VPCast & WeightedPPersistence)..."
$OPP_CMD -c VPCast > results/log_VPCast.txt 2>&1 &
$OPP_CMD -c wPPersistence > results/log_weighted.txt 2>&1 &
wait

echo "All high-density simulations complete! Check the results folder."
