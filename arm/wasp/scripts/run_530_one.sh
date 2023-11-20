#!/bin/bash

###############################################################################
# Script to run Figure 9 Evaluation of the paper
# 
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables 
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe, 
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

#echo "************************************************************************"
#echo "ASPLOS'20 - Artifact Evaluation - Mitosis - Figure 9"
#echo "************************************************************************"

#ROOT=$(dirname `readlink -f "$0"`)
#source $ROOT/site_config.sh

#PERF_EVENTS=cycles,dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses,dtlb_load_misses.walk_duration,dtlb_store_misses.walk_duration
#PERF_EVENTS=cycles,dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses,dtlb_load_misses.walk_duration,dtlb_store_misses.walk_duration,page_walker_loads.dtlb_l1,page_walker_loads.dtlb_l2,page_walker_loads.dtlb_l3,page_walker_loads.dtlb_memory,page_walker_loads.dtlb_l1,page_walker_loads.dtlb_l2,page_walker_loads.dtlb_l3,page_walker_loads.dtlb_memory,LLC-loads,LLC-load-misses,LLC-stores,LLC-store-misses
PERF_EVENTS=cycles,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,context-switches,cpu-migrations,page-faults,mem_access,remote_access,instructions,branch-misses,bus-cycles,cache-misses,cache-references,stalled-cycles-backend,stalled-cycles-frontend,alignment-faults,major-faults,minor-faults,branch-loads,branch-load-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,memory_error,remote_access_rd,ll_cache,ll_cache,ll_cache_miss,ll_cache_miss_rd,bus_access
NR_PTCACHE_PAGES=1100000 # --- 2GB per socket
# XSBENCH_ARGS=" -- -p 25000000 -g 920000 "
# XSBENCH_ARGS=" -- -p 15000000 -g 180000 "
XSBENCH_ARGS=" -- -p 15000000 -g 170000 "
# GRAPH500_ARGS=" -- -s 29 -e 21"
GRAPH500_ARGS=" -- -s 27 -e 21"
BENCH_ARGS=""
SYSBENCH_ARGS=" --threads=96 --memory-block-size=1k --memory-total-size=128G memory run"
#CANNEAL_ARGS=" -- 96 150000 2000 /home/huawei/gitclone/datasets/canneal_small 500 "
CANNEAL_ARGS=" -- 96 150000 2000 /home/huawei/wasp/canneal_80G 500 "
#CANNEAL_ARGS=" -- 96 15000 2000 /home/huawei/gitclone/datasets/400000.nets 128 "
#BENCH_size="small"
BENCH_size="large"
if [ $BENCH_size == "small" ]; then
        BTREE_ARGS=""
        HASH_ARGS=""
else 
        BTREE_ARGS=" -- -n 1600000000 -l 18000000000 -o 4"
        #BTREE_ARGS=" -- -n 1400000000 -l 100000000 -o 4"  #140 能吃满80多G  35能吃满21G
        #HASH_ARGS=" -- -o 100000000 -i 10000000 -s 10000000"
 	HASH_ARGS=" -- -o 700000000 -i 20000000 -s 20000000" 	#100000000  大概是12GB
fi


#***********************Script-Arguments***********************
# if [ $# -ne 2 ]; then
# 	echo "Run as: $0 benchmark config"
# 	exit
# fi

BENCHMARK=$1
CONFIG=$2
NAME=$3

validate_benchmark_config()
{
	CURR_BENCH=$1
	CURR_CONFIG=$2

	if [ $CURR_BENCH == "memcached" ] || [ $CURR_BENCH == "xsbench" ] || [ $CURR_BENCH == "graph500" ] ||
		[ $CURR_BENCH == "hashjoin" ] || [ $CURR_BENCH == "btree" ] || [ $CURR_BENCH == "canneal" ] || [ $CURR_BENCH == "sysbench" ]; then
		: #echo "Benchmark: $CURR_BENCH"
	else
		echo "Invalid benchmark: $CURR_BENCH"
		exit
	fi
        FIRST_CHAR=${CURR_CONFIG:0:1}
        if [ $FIRST_CHAR == "T" ]; then
                CURR_CONFIG=${CURR_CONFIG:1}
        fi
	if [ $CURR_CONFIG == "F" ] || [ $CURR_CONFIG == "FM" ] || [ $CURR_CONFIG == "FA" ] ||
		[ $CURR_CONFIG == "FAM" ] || [ $CURR_CONFIG == "I" ] || [ $CURR_CONFIG == "IM" ]; then
		: #echo "Config: $CURR_CONFIG"
	else
		echo "Invalid config: $CURR_CONFIG"
		exit
	fi
}

prepare_benchmark_name()
{
	PREFIX="bench_"
        POSTFIX="_mt"
	BIN=$PREFIX
	BIN+=$BENCHMARK
	BIN+=$POSTFIX
}

test_and_set_pathnames()
{
	SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
	ROOT="$(dirname "$SCRIPTS")"
	BENCHPATH=$ROOT"/bin/$BIN"
	PERF=$ROOT"/bin/perf"
	NUMACTL=$ROOT"/bin/numactl"
        ICOLLECTOR=$ROOT"/bin/icollector"
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
	DATADIR=$ROOT"/evaluation/measured/figure9/$BENCHMARK"
        RUNDIR=$DATADIR/$(hostname)-config-$BENCHMARK-$CONFIG-$(date +"%Y%m%d-%H%M%S")
	mkdir -p $RUNDIR
        if [ $? -ne 0 ]; then
                echo "Error creating output directory: $RUNDIR"
        fi
	OUTFILE=$RUNDIR/perflog-$BENCHMARK-$(hostname)-$CONFIG.dat
}

test_and_set_configs()
{
        CURR_CONFIG=$1
        FIRST_CHAR=${CURR_CONFIG:0:1}
        thp="never"
        if [ $FIRST_CHAR == "T" ]; then
                thp="always"
        fi
        echo $thp | sudo tee /sys/kernel/mm/transparent_hugepage/enabled > /dev/null
        if [ $? -ne 0 ]; then
                echo  "ERROR setting thp to: $thp"
                exit
        fi
        echo $thp | sudo tee /sys/kernel/mm/transparent_hugepage/defrag > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting thp to: $thp"
                exit
        fi

        AUTONUMA="0"
        if [ $CURR_CONFIG == "FA" ] || [ $CURR_CONFIG == "FAM" ] || [ $CURR_CONFIG == "TFA" ] || [ $CURR_CONFIG == "TFAM" ]; then
                AUTONUMA="1"
        fi
        echo $AUTONUMA | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting AutoNUMA to: $AUTONUMA"
                exit
        fi
        # obtain the number of available nodes
        NODESTR=$(numactl --hardware | grep available)
        echo $NODESTR
        NODE_MAX=$(echo ${NODESTR##*: } | cut -d " " -f 1)
        echo $NODE_MAX
        NODE_MAX=`expr $NODE_MAX - 1`
        echo $NODE_MAX
        CMD_PREFIX=$NUMACTL

        # --- check interleaving
        if [ $CURR_CONFIG == "I" ] || [ $CURR_CONFIG == "IM" ] || [ $CURR_CONFIG == "TI" ] || [ $CURR_CONFIG == "TIM" ]; then
                CMD_PREFIX+=" --interleave=all"
        fi

        # --- check page table replication
        # LAST_CHAR="${CURR_CONFIG: -1}"
        # if [ $LAST_CHAR == "M" ]; then
        #         CMD_PREFIX+=" --pgtablerepl=$NODE_MAX "
        #         echo 0 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
        #         if [ $? -ne 0 ]; then
        #                 echo "ERROR setting pgtable_replication to $0"
        #                 exit
        #         fi
        #         # --- drain first then reserve
        #         echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
        #         if [ $? -ne 0 ]; then
        #                 echo "ERROR setting pgtable_replication_cache to $0"
        #                 exit
        #         fi
        #         echo $NR_PTCACHE_PAGES | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
        #         if [ $? -ne 0 ]; then
        #                 echo "ERROR setting pgtable_replication_cache to $NR_PTCACHE_PAGES"
        #                 exit
        #         fi
        # else
	# 	#CMD_PREFIX+=" --pgtablerepl=$NODE_MAX "
        #         # --- enable default page table allocation
        #         echo -1 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
        #         if [ $? -ne 0 ]; then
        #                 echo "ERROR setting pgtable_replication to -1"
        #                 exit
        #         fi
        #         # --- drain page table cache
        #         echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
        #         if [ $? -ne 0 ]; then
        #                 echo "ERROR setting pgtable_replication to 0"
        #                 exit
        #         fi
        # fi

        if [ $BENCHMARK == "xsbench" ]; then
                BENCH_ARGS=$XSBENCH_ARGS
        elif [ $BENCHMARK == "graph500" ]; then
                BENCH_ARGS=$GRAPH500_ARGS
        elif [ $BENCHMARK == "btree" ]; then
                BENCH_ARGS=$BTREE_ARGS
        elif [ $BENCHMARK == "hashjoin" ]; then
                BENCH_ARGS=$HASH_ARGS
        elif [ $BENCHMARK == "canneal" ]; then
                BENCH_ARGS=$CANNEAL_ARGS
        fi

}

prepare_datasets()
{
	SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
        ROOT="$(dirname "$SCRIPTS")"
	# --- only for canneal and liblinear
	if [ $1 == "canneal" ]; then
		$ROOT/datasets/prepare_canneal_datasets.sh large
	fi
}

launch_benchmark_config()
{
	# --- clean up exisiting state/processes
	rm /tmp/alloctest-bench.ready &>/dev/null
	rm /tmp/alloctest-bench.done &> /dev/null
	killall bench_stream &>/dev/null
	LAUNCH_CMD="$CMD_PREFIX $BENCHPATH $BENCH_ARGS"
	echo $LAUNCH_CMD >> $OUTFILE
	$LAUNCH_CMD >> /var/log/syslog & #> /dev/null 2>&1 &
	BENCHMARK_PID=$!
        echo -e "\e[0mWaiting for benchmark: $BENCHMARK_PID to be ready"
	echo -e "\e[0mWaiting for benchmark: $BENCHMARK_PID to be ready" >> /var/log/syslog
	while [ ! -f /tmp/alloctest-bench.ready ]; do
		sleep 0.1
	done
	SECONDS=0
	$PERF stat -x, -o $OUTFILE --append -e $PERF_EVENTS -p $BENCHMARK_PID &
	PERF_PID=$!
        $ICOLLECTOR $BENCHMARK_PID perf-$NAME.log > /dev/null 2>&1 &
        icollector_pid=$!
        current=`date "+%Y-%m-%d %H:%M:%S"` 
        echo -e "\e[0mWaiting for benchmark to be done current :$current"
	echo -e "\e[0mWaiting for benchmark to be done current :$current" >> /var/log/syslog
	while [ ! -f /tmp/alloctest-bench.done ]; do
		sleep 0.1
	done
	DURATION=$SECONDS
	kill -INT $PERF_PID &> /dev/null
	wait $PERF_PID
        kill -INT $icollector_pid &> /dev/null
        wait $icollector_pid
	wait $BENCHMARK_PID 2>/dev/null
        echo "Execution Time (seconds): $DURATION"
        echo "Execution Time (seconds): $DURATION" >> /var/log/syslog 
	echo "Execution Time (seconds): $DURATION" >> $OUTFILE 
        echo "****success****" >> /var/log/syslog
	echo "****success****" >> $OUTFILE
        echo "$BENCHMARK : $CONFIG completed."
	echo "$BENCHMARK : $CONFIG completed." >> /var/log/syslog
        current=`date "+%Y-%m-%d %H:%M:%S"`  
        # timeStamp=`date -d "$current" +%s`   
        #将current转换为时间戳，精确到毫秒  
        # currentTimeStamp=$((timeStamp*1000+`date "+%N"`/1000000)) 
        echo "current = "$current >> /var/log/syslog
        # echo "timestap = "$currentTimeStamp >> /var/log/syslog
        echo ""
        # kill -INT $BENCHMARK_PID &> /dev/null
	killall bench_stream &>/dev/null
}

# --- prepare the setup
validate_benchmark_config $BENCHMARK $CONFIG
prepare_benchmark_name $BENCHMARK
test_and_set_pathnames
test_and_set_configs $CONFIG
# prepare_datasets $BENCHMARK
# --- finally, launch the job
launch_benchmark_config
