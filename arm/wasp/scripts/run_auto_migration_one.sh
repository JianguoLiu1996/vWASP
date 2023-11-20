#!/bin/bash

###############################################################################
# Script to run Figure 6 & 10 Evaluation of the paper
# 
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables 
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe, 
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

#echo "************************************************************************"
#echo "ASPLOS'20 - Artifact Evaluation - Mitosis - Figure 6, 10"
#echo "************************************************************************"

ROOT=$(dirname `readlink -f "$0"`)
MAIN="$(dirname "$ROOT")"
#source $ROOT/site_config.sh
PERF_EVENTS=dTLB-load-misses,iTLB-load-misses,dtlb_walk,itlb_walk,context-switches,cpu-migrations,page-faults,mem_access,remote_access,instructions,branch-misses,bus-cycles,cache-misses,cache-references,stalled-cycles-backend,stalled-cycles-frontend,alignment-faults,major-faults,minor-faults,branch-loads,branch-load-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,memory_error,remote_access_rd,ll_cache,ll_cache,ll_cache_miss,ll_cache_miss_rd,bus_access
# PERF_EVENTS=cycles,dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses,dtlb_load_misses.walk_duration,dtlb_store_misses.walk_duration,page_walker_loads.dtlb_l1,page_walker_loads.dtlb_l2,page_walker_loads.dtlb_l3,page_walker_loads.dtlb_memory,page_walker_loads.dtlb_l1,page_walker_loads.dtlb_l2,page_walker_loads.dtlb_l3,page_walker_loads.dtlb_memory,LLC-loads,LLC-load-misses,LLC-stores,LLC-store-misses
#XSBENCH_ARGS=" -- -t 16 -g 180000 -p 15000000" #原始配置
XSBENCH_ARGS=" -- -t 1 -g 40000 -p 1500000"
LIBLINEAR_ARGS=" -- -s 6 -n 28 $MAIN/datasets/kdd12 "
#CANNEAL_ARGS=" -- 1 150000 2000 $MAIN/datasets/canneal_small 500 "
CANNEAL_ARGS=" -- 1 150000 2000 /home/huawei/wasp/canneal_20G 500 "
#CANNEAL_ARGS=" -- 1 150000 2000 /home/huawei/gitclone/datasets/400000.nets 500 "
#CANNEAL_ARGS=" -- 1 150000 2000 /home/huawei/gitclone/datasets/2500000.nets 500 "
BENCH_ARGS=""

BTREE_ARGS=" -- -n 370000000 -l 20000000000 -o 4"  #140 能吃满80多G  35能吃满21G
HASH_ARGS=" -- -o 100000000 -i 20000000 -s 20000000"

NR_PTCACHE_PAGES=1100000 # --- 2GB per socket

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
        FIRST_CHAR=${CURR_CONFIG:0:1}
        if [ $FIRST_CHAR == "T" ]; then
                CURR_CONFIG=${CURR_CONFIG:1}
        fi
        LAST_CHAR=${CURR_CONFIG: -1}
        if [ $LAST_CHAR == "M" ]; then
                CURR_CONFIG=${CURR_CONFIG::-1}
        fi
        if [ $LAST_CHAR == "A" ]; then
                CURR_CONFIG=${CURR_CONFIG::-1}
        fi
	if [ $CURR_BENCH == "gups" ] || [ $CURR_BENCH == "btree" ] || [ $CURR_BENCH == "hashjoin" ] ||
		[ $CURR_BENCH == "redis" ] || [ $CURR_BENCH == "xsbench" ] || [ $CURR_BENCH == "pagerank" ] ||
		[ $CURR_BENCH == "liblinear" ] || [ $CURR_BENCH == "canneal" ]; then
		: #echo "Benchmark: $CURR_BENCH"
	else
		echo "Invalid benchmark: $CURR_BENCH"
		exit
	fi

	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "LPRD" ] || [ $CURR_CONFIG == "LPRDI" ] ||
		[ $CURR_CONFIG == "RPLD" ] || [ $CURR_CONFIG == "RPILD" ] || [ $CURR_CONFIG == "RPRD" ] ||
		[ $CURR_CONFIG == "RPIRDI" ] || [ $CURR_CONFIG == "RPILDM" ]|| [ $CURR_CONFIG == "RPLDI" ]|| [ $CURR_CONFIG == "RPLDIM" ]; then
		: #echo "Config: $CURR_CONFIG"
	else
		echo "Invalid config: $CURR_CONFIG"
		exit
	fi
}

prepare_benchmark_name()
{
	if [ $1 == "gups" ] || 	[ $1 == "btree" ] || [ $1 == "redis" ] || [ $1 == "hashjoin" ]; then
		POSTFIX="_st"
	else
		POSTFIX="_mt"
	fi
	PREFIX="bench_"
        #POSTFIX="_toy"
	BIN=$PREFIX
	BIN+=$BENCHMARK
	BIN+=$POSTFIX
}
reset_configs()
{
	echo "disabling gPT Replication and Migration"
	echo 0 | sudo tee /proc/sys/kernel/numa_pgtable_migration > /dev/null
	echo 0 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
}
#prepare_basic_config_params()
#{
#	CURR_CONFIG=$1
#	# --- setup page table node
#	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "LPRD" ] || [ $CURR_CONFIG == "LPRDI" ]; then
#		PT_NODE=0
#	else
#		PT_NODE=1
#	fi
#
#	# --- setup data node
#	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "RPLD" ] || [ $CURR_CONFIG == "RPILD" ]; then
#		DATA_NODE=0
#	else
#		DATA_NODE=1
#	fi
#
#	# --- setup cpu node
#		CPU_NODE=0
#
#	# --- setup mitosis
#	if [ $CURR_CONFIG == "RPILDM" ]; then
#		PT_NODE=1
#		DATA_NODE=0
#		MITOSIS=1
#	fi
#
#	# --- setup interference node
#	INT_NODE=1
#}

prepare_basic_config_params()
{
	CURR_CONFIG=$1
        FIRST_CHAR=${CURR_CONFIG:0:1}
        if [ $FIRST_CHAR == "T" ]; then
                CURR_CONFIG=${CURR_CONFIG:1}
        fi
        LAST_CHAR=${CURR_CONFIG: -1}
        if [ $LAST_CHAR == "M" ]; then
                CURR_CONFIG=${CURR_CONFIG::-1}
        fi
        if [ $LAST_CHAR == "A" ]; then
                CURR_CONFIG=${CURR_CONFIG::-1}
        fi

        PT_NODE=0
	# --- setup data node
        DATA_NODE=3
	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "RPRD" ] || [ $CURR_CONFIG == "RPIRDI" ]; then
		DATA_NODE=0
	fi

	# --- setup cpu node
        CPU_NODE=3
	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "LPRD" ] || [ $CURR_CONFIG == "LPRDI" ]; then
                CPU_NODE=0
        fi
	# --- setup mitosis
	if [ $LAST_CHAR == "M" ]; then
		MITOSIS=3
                CPU_NODE=3
                DATA_NODE=3
	fi
        if [ $LAST_CHAR == "A" ]; then
		MITOSIS=3
                CPU_NODE=3
                DATA_NODE=3
	fi

        if [ $CURR_CONFIG == "LPLD" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=-6
		echo "qu : config is LPLD"
        fi

	if [ $1 == "RPILD" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=-3
		echo "qu : config is RPILD"
        fi
	
	if [ $1 == "RPILDM" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=0
                echo "qu : config is RPILDM"
        fi

        if [ $1 == "RPILDA" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=0
                echo "qu : config is RPILDA"
        fi

        if [ $1 == "RPLD" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=-3
		echo "qu : config is RPLD"
        fi
	
	if [ $1 == "RPLDM" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=0
                echo "qu : config is RPLDM"
        fi

	if [ $1 == "LPRD" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=-3
		echo "qu : config is LPRD"
	fi

        #CONFIGS="RPLDI RPLDIM"
        if [ $1 == "RPLDI" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=-3
		echo "qu : config is RPLDI"
        fi
	
	if [ $1 == "RPLDIM" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=0
                echo "qu : config is RPLDIM"
        fi

        if [ $1 == "RPLDIA" ]; then
                CPU_NODE=3
                DATA_NODE=3
                PT_NODE=-3
                echo "qu : config is RPLDIA"
        fi
        

	# --- setup interference node
	INT_NODE=0
        if [ $CURR_CONFIG == "LPRDI" ] || [ $CURR_CONFIG == "RPLDI" ]; then
                echo "qu : INT_NODE is 3"
                INT_NODE=3
        fi

        if [ $BENCHMARK == "xsbench" ]; then
                BENCH_ARGS=$XSBENCH_ARGS
        elif [ $BENCHMARK == "liblinear" ]; then
                BENCH_ARGS=$LIBLINEAR_ARGS
        elif [ $BENCHMARK == "btree" ]; then
                BENCH_ARGS=$BTREE_ARGS
        elif [ $BENCHMARK == "hashjoin" ]; then
                BENCH_ARGS=$HASH_ARGS
        elif [ $BENCHMARK == "canneal" ]; then
                BENCH_ARGS=$CANNEAL_ARGS
        fi
}

prepare_all_pathnames()
{
	SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
	ROOT="$(dirname "$SCRIPTS")"
	BENCHPATH=$ROOT"/bin/$BIN"
	PERF=$ROOT"/bin/perf"
	INT_BIN=$ROOT"/bin/bench_stream"
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
        if [ ! -e $INT_BIN ]; then
            echo "Interference binary is missing"
            exit
        fi
        # where to put the output file (based on CONFIG)
        DIR_SUFFIX=6
        FIRST_CHAR=${CONFIG:0:1}
        if [ $CONFIG == "RPILDM" ] || [ $FIRST_CHAR == "T" ]; then
                DIR_SUFFIX=10
        
        fi
	DATADIR=$ROOT"/evaluation/measured/figure$DIR_SUFFIX/$BENCHMARK"
        thp=$(cat /sys/kernel/mm/transparent_hugepage/enabled)
        thp=$(echo $thp | awk '{print $1}')
        RUNDIR=$DATADIR/$(hostname)-config-$BENCHMARK-$CONFIG-$(date +"%Y%m%d-%H%M%S")

	mkdir -p $RUNDIR
        if [ $? -ne 0 ]; then
                echo "Error creating output directory: $RUNDIR"
        fi
	OUTFILE=$RUNDIR/perflog-$BENCHMARK-$(hostname)-$CONFIG.dat
}

set_system_configs()
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
        echo 1 | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting AutoNUMA to: 0"
                exit
        fi
        echo $PT_NODE | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting pgtable allocation to node: $PT_NODE"
                exit
        fi

        # --- check page table replication
        LAST_CHAR="${CURR_CONFIG: -1}"
        if [ $LAST_CHAR == "M" ]; then
                # echo "--- drain first then reserve"
                # # --- drain first then reserve
                # echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
                # if [ $? -ne 0 ]; then
                #         echo "ERROR setting pgtable_replication_cache to $0"
                #         exit
                # fi
                # echo $NR_PTCACHE_PAGES | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
                # if [ $? -ne 0 ]; then
                #         echo "ERROR setting pgtable_replication_cache to $NR_PTCACHE_PAGES"
                #         exit
                # fi
                echo "enabling gPT migration"
		echo 1 | sudo tee /proc/sys/kernel/numa_pgtable_migration > /dev/null
	fi

        # if [ $LAST_CHAR == "A" ]; then
        #         # --- drain first then reserve
        #         echo "--- drain first then reserve"
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
	fi
}

launch_interference()
{
	CURR_CONFIG=$1
        LAST_CHAR=${CURR_CONFIG: -1}
        if [ $LAST_CHAR == "M" ]; then
            CURR_CONFIG=${CURR_CONFIG::-1}
        fi
        if [ $LAST_CHAR == "A" ]; then
            CURR_CONFIG=${CURR_CONFIG::-1}
        fi
	FIRST_CHAR=${CURR_CONFIG:0:1}
	if [ $FIRST_CHAR == "T" ]; then
		CURR_CONFIG=${CURR_CONFIG:1}
	fi
	if [ $CURR_CONFIG == "LPRDI" ] || [ $CURR_CONFIG == "RPILD" ] || [ $CURR_CONFIG == "RPIRDI" ] || [ $CURR_CONFIG == "RPLDI" ]; then
		$NUMACTL -c $INT_NODE -m $INT_NODE $INT_BIN > /dev/null 2>&1 &
		if [ $? -ne 0 ]; then
			echo "Failure launching interference."
			exit
		fi
	fi
}

prepare_datasets()
{
	SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
        ROOT="$(dirname "$SCRIPTS")"
	# --- only for canneal and liblinear
	if [ $1 == "canneal" ]; then
		$ROOT/datasets/prepare_canneal_datasets.sh small
	elif [ $1 == "liblinear" ]; then
		$ROOT/datasets/prepare_liblinear_dataset.sh
	fi
}

launch_benchmark_config()
{
        # CURR_CONFIG=$1
        # LAST_CHAR="${CURR_CONFIG: -1}"
	# --- clean up exisiting state/processes
	rm /tmp/alloctest-bench.ready &>/dev/null
	rm /tmp/alloctest-bench.done &> /dev/null
	killall bench_stream &>/dev/null

        CMD_PREFIX=$NUMACTL
        CMD_PREFIX+=" -m $DATA_NODE -c $CPU_NODE "
        LAST_CHAR=${CONFIG: -1}
        # obtain the number of available nodes
        NODESTR=$(numactl --hardware | grep available)
        NODE_MAX=$(echo ${NODESTR##*: } | cut -d " " -f 1)
        NODE_MAX=`expr $NODE_MAX - 1`
        if [ $LAST_CHAR == "M" ]; then
                CMD_PREFIX+=" --pgtablerepl=$NODE_MAX"
        fi
	LAUNCH_CMD="$CMD_PREFIX $BENCHPATH $BENCH_ARGS"
	echo $LAUNCH_CMD >> $OUTFILE
	$LAUNCH_CMD > /dev/null 2>&1 &
        outputfile=$BENCHPATH
        str1=".txt"
	outputfile+=$CONFIG
	outputfile+=$str1
	BENCHMARK_PID=$!
        SAMPPLING_CMD="$ICOLLECTOR $BENCHMARK_PID $outputfile"
        if [ $LAST_CHAR == "A" ]; then
                SAMPPLING_CMD+=" 0 10 1"    
        else
                SAMPPLING_CMD+=" 0 10 0"
        fi
        echo  "sample cmd = $SAMPPLING_CMD"
        $SAMPPLING_CMD &
        SAMPPLING_PID=$!
	echo -e "\e[0mWaiting for benchmark: $BENCHMARK_PID to be ready"
	while [ ! -f /tmp/alloctest-bench.ready ]; do
		sleep 0.1
	done
	SECONDS=0
	launch_interference $CONFIG
	$PERF stat -x, -o $OUTFILE --append -e $PERF_EVENTS -p $BENCHMARK_PID &
	PERF_PID=$!
        # $ICOLLECTOR $BENCHMARK_PID perf-$NAME.log > /dev/null 2>&1 &
        # icollector_pid=$!
	echo -e "\e[0mWaiting for benchmark to be done"
	while [ ! -f /tmp/alloctest-bench.done ]; do
		sleep 0.1
	done
	DURATION=$SECONDS
	kill -INT $PERF_PID &> /dev/null
	wait $PERF_PID
        kill -INT $SAMPPLING_PID &> /dev/null
        wait $SAMPPLING_PID
	wait $BENCHMARK_PID 2>/dev/null
	echo "Execution Time (seconds): $DURATION" >> $OUTFILE
	echo "****success****" >> $OUTFILE
	echo "$BENCHMARK : $CONFIG completed."
        echo ""
	killall bench_stream &>/dev/null
}

# --- prepare setup
validate_benchmark_config $BENCHMARK $CONFIG
prepare_benchmark_name $BENCHMARK
reset_configs
prepare_basic_config_params $CONFIG
prepare_all_pathnames
#prepare_datasets $BENCHMARK
set_system_configs $CONFIG

# --- finally, launch the job
launch_benchmark_config $CONFIG
