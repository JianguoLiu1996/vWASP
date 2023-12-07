#!/bin/bash

###############################################################################
# Script to run Figure 9 Evaluation of the paper
# 
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables 
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe, 
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

# echo "************************************************************************"
echo "mytest start:"
# echo "************************************************************************"

ROOT=$(dirname `readlink -f "$0"`)

test_and_set_pathnames()
{
	SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
	# ROOT="$(dirname "$SCRIPTS")"
	ROOT=$(dirname `readlink -f "$0"`)
	BENCHPATH=$ROOT"/bin/$BIN"
	PERF=$ROOT"/bin/perf"
	NUMACTL=$ROOT"/bin/numactl"
    AUTOCONFIG=$ROOT"/bin/icollector"
	LMBENCH=$ROOT"/bin/lat_mem_rd"
	# LMBENCH=$ROOT"/lmbench-3.0-a9/bin/lat_mem_rd"
	LMBENCH_CON1="-W 5 -N 5"
	LMBENCH_CON2="-t 64M"
	BENCH_CONF1="-c"
	BENCH_CONF2="-m"
	NODE1Log=$ROOT"/bin/nodes_tmp.csv"
	NODELog=$ROOT"/bin/nodes.csv"
        if [ ! -e $BENCHPATH ]; then
                echo "Benchmark binary is missing"
                exit
        fi
        if [ ! -e $PERF ]; then
                echo "Perf binary is missing"
                exit
        fi
        if [ ! -e $NUMACTL ]; then
                echo "numactl is missing"
                exit
        fi
	# CMD_PREFIX=$NUMACTL
	# CMD_PREFIX+=$BENCH_CONF
	
	# $LAUNCH_CMD >> $NODE1Log & #> /dev/null 2>&1 &
	# BENCHMARK_PID=$!
	# echo "mytest done!"
	# DATADIR=$ROOT"/evaluation/measured/figure9/$BENCHMARK"
    #     RUNDIR=$DATADIR/$(hostname)-config-$BENCHMARK-$CONFIG-$(date +"%Y%m%d-%H%M%S")
	# mkdir -p $RUNDIR
    #     if [ $? -ne 0 ]; then
    #             echo "Error creating output directory: $RUNDIR"
    #     fi
	# OUTFILE=$RUNDIR/perflog-$BENCHMARK-$(hostname)-$CONFIG.dat
    #     WASP_OUTFILE=$RUNDIR/waspflog-$BENCHMARK-$(hostname)-$CONFIG.dat
flag_str=""	
numone=1
SECONDS=0
# for runtimes in $(seq 4) 
# rm $NODE1Log  #先清理以前的记录
# rm $NODELog
while true
do
for fromNode in $(seq 4) 
do
	fromNodeflag=$(expr $fromNode - $numone)
	for toNode in $(seq 4) 
	do
		toNodeflag=$(expr $toNode - $numone)
		flag_str=$fromNodeflag;
		flag_str+="to";
		flag_str+=$toNodeflag;
		# echo "Start test round: $fromNodeflag to $toNodeflag, and flag = $flag_str"
		LAUNCH_CMD="$NUMACTL $BENCH_CONF1 $fromNodeflag $BENCH_CONF2 $toNodeflag $LMBENCH $LMBENCH_CON1 -S $flag_str  -O $NODE1Log $LMBENCH_CON2"
		# echo $LAUNCH_CMD
		$LAUNCH_CMD &
		BENCHMARK_PID=$!
		# echo $BENCHMARK_PID
	done
done
sleep 20
echo "Execution Time (seconds): $SECONDS"
echo "mytest done!"
cp $NODE1Log $NODELog
rm $NODE1Log
done
}

test_and_set_pathnames
