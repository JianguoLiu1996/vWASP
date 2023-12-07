#!/bin/bash

###############################################################################
# Script to run Figure 10 Evaluation of the paper
# 
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables 
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe, 
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

echo "************************************************************************"
echo "ASPLOS'20 - Artifact Evaluation - Mitosis - Figure 10A"
echo "************************************************************************"

ROOT=$(dirname `readlink -f "$0"`)
#source $ROOT/site_config.sh

# List of all benchmarks to run
#BENCHMARKS="gups btree hashjoin redis xsbench canneal"
#BENCHMARKS="btree"
#BENCHMARKS="hashjoin"
#BENCHMARKS="redis"
#BENCHMARKS="xsbench"
#BENCHMARKS="canneal"
BENCHMARKS="gups"
# List of all configs to run
#CONFIGS="LPLD RPILD RPILDA RPILDM RPILDAM"
#CONFIGS="RPILD RPILDM"
# CONFIGS="LPLD"
#CONFIGS="RPILD"
CONFIGS="RPILDM"
echo "myNewTest: version 1"
for RUNTIMES in $(seq 1) 
do
	echo "Start test round: $RUNTIMES"
	# echo "Start test round: $RUNTIMES" >> /var/log/syslog 
    for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "******************$bench : $config***********************"
			bash $ROOT/run_autoconfig_migration_one.sh $bench $config
		done
		# sleep 20s
	done
done

# --- process the output logs
$ROOT/process_logs_core.py --quiet
