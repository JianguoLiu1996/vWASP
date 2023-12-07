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
# BENCHMARKS="gups btree hashjoin redis xsbench pagerank liblinear canneal"
BENCHMARKS="gups btree hashjoin redis xsbench canneal"
#BENCHMARKS="btree"
#BENCHMARKS="hashjoin"
#BENCHMARKS="redis"
#BENCHMARKS="xsbench"
#BENCHMARKS="canneal"
#BENCHMARKS="gups"
# List of all configs to run
#CONFIGS="LPLD RPILD RPILDM RPLD RPLDM"
CONFIGS="LPLD"
#CONFIGS="LPLD RPILD RPILDM RPLD RPLDM RPLDI RPLDIM"
#CONFIGS="LPLD RPILDA RPILDM RPLDI RPLDIM RPLDIA"
#CONFIGS="LPLD RPILD RPILDM"
#CONFIGS="RPILD RPILDM"
# CONFIGS="LPLD RPLDI RPLDIM"
#CONFIGS="RPLD"
#CONFIGS="LPLD"
#CONFIGS="RPILD"
#CONFIGS="RPILDM"
#CONFIGS="TLPLD TRPILD TRPILDM"
#CONFIGS="TLPLD"
#CONFIGS="TRPILD TRPILDM"

for RUNTIMES in $(seq 1) 
do
	echo "Start test round: $RUNTIMES"
	echo "Start test round: $RUNTIMES" >> /var/log/syslog 
    for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "******************$bench : $config***********************"
			bash $ROOT/run_auto_migration_one.sh $bench $config $bench-$config-$RUNTIMES
			sync
			sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
			echo "wait  10s ......"
			sleep 10
		done
	done
done

# --- process the output logs
$ROOT/process_logs_core.py --quiet
