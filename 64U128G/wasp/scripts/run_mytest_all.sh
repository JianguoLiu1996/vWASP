#!/bin/bash

###############################################################################
# Script to run Figure 9 Evaluation of the paper
# 
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables 
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe, 
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

echo "************************************************************************"
echo "ASPLOS'20 - Artifact Evaluation - Mitosis - Figure 9A"
echo "************************************************************************"

ROOT=$(dirname `readlink -f "$0"`)
#source $ROOT/site_config.sh

# List of all benchmarks to run
#BENCHMARKS="xsbench graph500 hashjoin btree canneal"
#BENCHMARKS="hashjoin btree canneal"
#BENCHMARKS="btree hashjoin"
#BENCHMARKS="btree canneal"
#BENCHMARKS="hashjoin"
#BENCHMARKS="canneal"
# BENCHMARKS="xsbench"
BENCHMARKS="graph500"
# BENCHMARKS="btree"
#BENCHMARKS="sysbench"
# List of all configs to run
#CONFIGS="F FM FA FAM I IM"
CONFIGS="F FM FA FW FAW"
#CONFIGS="FA FAW"
# CONFIGS="FAW"
# CONFIGS="FA"
#CONFIGS="TF TFM TI TIM"
#CONFIGS="TF TFM"
# CONFIGS="FM"
# CONFIGS="FW"
#CONFIGS="TFW"
# CONFIGS="F"
#CONFIGS="TF TFM TFW"
#CONFIGS="TIM"
#CONFIGS="IM"
#CONFIGS="IW"
#CONFIGS="F FM I IM"
# CONFIGS="F FM"
#CONFIGS="I IM IW"
#CONFIGS="TIM TI"

for RUNTIMES in $(seq 3) 
do
	echo "Start test round: $RUNTIMES"
	echo "Start test round: $RUNTIMES" >> /var/log/syslog 
    for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "******************$bench : $config***********************"
			bash $ROOT/run_mytest_one.sh $bench $config $bench-$config-$RUNTIMES
			bash $ROOT/clear.sh
		done
	done
done

echo "******************ALL done : suncess!***********************"
# --- process the output logs
$ROOT/process_logs_core.py --quiet
