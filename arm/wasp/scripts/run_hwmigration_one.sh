#!/bin/bash

#PT_DUMP="/home/huawei/experiment/dumptest/bin/page-table-dump"
# sudo bash set_pt_cache.sh m



# if [ $# -ne 5 ];then
#     echo "input the name"
#     exit
# fi
KEY=$2
DATA_SIZE=$3
REQUEST=`expr $KEY / 100`
TIME=$6

#PERF_EVENTS=cycles,dTBL-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,dtlb_walk,itlb_walk,context-switches,cpu-migrations,page-faults,mem_access,remote_access

PERF_UNCORE_EVENTS=hisi_sccl1_ddrc2/flux_wr/,hisi_sccl3_ddrc3/flux_wr/,hisi_sccl5_ddrc2/flux_wr/,hisi_sccl7_ddrc3/flux_wr/,hisi_sccl1_ddrc2/flux_rd/,hisi_sccl3_ddrc3/flux_rd/,hisi_sccl5_ddrc2/flux_rd/,hisi_sccl7_ddrc3/flux_rd/,hisi_sccl1_ddrc2/flux_wcmd/,hisi_sccl3_ddrc3/flux_wcmd/,hisi_sccl5_ddrc2/flux_wcmd/,hisi_sccl7_ddrc3/flux_wcmd/,hisi_sccl1_ddrc2/flux_rcmd/,hisi_sccl3_ddrc3/flux_rcmd/,hisi_sccl5_ddrc2/flux_rcmd/,hisi_sccl7_ddrc3/flux_rcmd/,hisi_sccl1_hha3/rx_ops_num/,hisi_sccl3_hha1/rx_ops_num/,hisi_sccl5_hha7/rx_ops_num/,hisi_sccl7_hha5/rx_ops_num/,hisi_sccl1_hha3/rx_outer/,hisi_sccl3_hha1/rx_outer/,hisi_sccl5_hha7/rx_outer/,hisi_sccl7_hha5/rx_outer/,hisi_sccl1_hha3/rx_sccl/,hisi_sccl3_hha1/rx_sccl/,hisi_sccl5_hha7/rx_sccl/,hisi_sccl7_hha5/rx_sccl/,cycles,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,dtlb_walk,itlb_walk,context-switches,cpu-migrations,page-faults,mem_access,remote_access,l1i_tlb,l1d_tlb,ll_cache_miss_rd,ll_cache_miss


BENCH_ARGS="-t 20 -c 5 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R --test-time=$TIME --pipeline=10000" 
DATA_LOAD="-t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000" 
Memcached_BENCH_ARGS="-p 6379 -P memcache_text -t 20 -c 5 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R --test-time=$TIME --pipeline=10000" 
Memcached_DATA_LOAD="-p 6379 -P memcache_text -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000" 
# BENCH_ARGS="-t 20 -c 5 -R --randomize --distinct-client-seed -d 64 --key-maximum=160000000 --key-minimum=1 --ratio=0:1 --key-pattern=R:R --test-time=900 --pipeline=10000" #��ȡ���ݲ���
# DATA_LOAD="-t 20 -c 5 -n 1600000 -R --randomize --distinct-client-seed -d 64 --key-maximum=160000000 --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000" #װ�����ݲ���
NR_PTCACHE_PAGES=262144 # --- 1GB per node

BENCHMARK=$5
CONFIG=$4
NAME=$1
# CURR_CONFIG=$4
# FIRST_CHAR=${CURR_CONFIG:0:1}
# # --- check page table replication
# LAST_CHAR="${CURR_CONFIG: -1}"

# validate_benchmark_config()
# {
# 	CURR_BENCH=$5
# 	CURR_CONFIG=$4
#         FIRST_CHAR=${CURR_CONFIG:0:1}
#         if [ $FIRST_CHAR == "T" ]; then
#                 CURR_CONFIG=${CURR_CONFIG:1}
#         fi
#         LAST_CHAR=${CURR_CONFIG: -1}
#         if [ $LAST_CHAR == "M" ]; then
#                 CURR_CONFIG=${CURR_CONFIG::-1}
#         fi
# 	if [ $CURR_BENCH == "gups" ] || [ $CURR_BENCH == "btree" ] || [ $CURR_BENCH == "hashjoin" ] ||
# 		[ $CURR_BENCH == "redis" ] || [ $CURR_BENCH == "xsbench" ] || [ $CURR_BENCH == "pagerank" ] ||
# 		[ $CURR_BENCH == "liblinear" ] || [ $CURR_BENCH == "canneal" ]; then
# 		: #echo "Benchmark: $CURR_BENCH"
# 	else
# 		echo "Invalid benchmark: $CURR_BENCH"
# 		exit
# 	fi

# 	if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "LPRD" ] || [ $CURR_CONFIG == "LPRDI" ] ||
# 		[ $CURR_CONFIG == "RPLD" ] || [ $CURR_CONFIG == "RPILD" ] || [ $CURR_CONFIG == "RPRD" ] ||
# 		[ $CURR_CONFIG == "RPIRDI" ] || [ $CURR_CONFIG == "RPILDM" ]; then
# 		: #echo "Config: $CURR_CONFIG"
# 	else
# 		echo "Invalid config: $CURR_CONFIG"
# 		exit
# 	fi
# }
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

        PT_NODE=0
	# --- setup data node
        DATA_NODE=3
        MITOSIS=3
	# if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "RPRD" ] || [ $CURR_CONFIG == "RPIRDI" ]; then
	# 	DATA_NODE=0
	# fi

	# # --- setup cpu node
        # CPU_NODE=3
	# if [ $CURR_CONFIG == "LPLD" ] || [ $CURR_CONFIG == "LPRD" ] || [ $CURR_CONFIG == "LPRDI" ]; then
        #         CPU_NODE=0
        # fi
	# # --- setup mitosis
	# if [ $LAST_CHAR == "M" ]; then
		
        #         CPU_NODE=3
        #         DATA_NODE=3
	# fi

  if [ $CURR_CONFIG == "LPLD" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=-6
		echo "qu : config is LPLD"
  fi

	if [ $CURR_CONFIG == "LPLDI" ]; then
		CPU_NODE=0
		DATA_NODE=0
		PT_NODE=-3
		echo "qu : config is LPLDI"
	fi

  if [ $CURR_CONFIG == "LPLDM" ]; then
    CPU_NODE=0
    DATA_NODE=0
    PT_NODE=0
		echo "qu : config is LPLDM"
  fi

	if [ $CURR_CONFIG == "RPILD" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=-3
		echo "qu : config is RPILD"
        fi
	
	if [ $CURR_CONFIG == "RPILDM" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=0
    echo "qu : config is RPILDM"
  fi

  if [ $CURR_CONFIG == "RPLD" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=-3
		echo "qu : config is RPLD"
  fi
	
  if [ $CURR_CONFIG == "RPRD" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=-3
		echo "qu : config is RPRD"
	fi
        
  if [ $CURR_CONFIG == "RPRDM" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=0
		echo "qu : config is RPRDM"
	fi

	if [ $CURR_CONFIG == "LPRD" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=-6
		echo "qu : config is RPLD"
	fi

	if [ $CURR_CONFIG == "LPRDM" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=0
		echo "qu : config is RPLDM"
	fi

	if [ $CURR_CONFIG == "RPIRD" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=-3
		echo "qu : config is RPIRD"
	fi

	if [ $CURR_CONFIG == "RPIRDM" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=0
		echo "qu : config is RPIRDM"
	fi

	if [ $CURR_CONFIG == "LPIRD" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=-3
		echo "qu : config is LPIRD"
	fi

	if [ $CURR_CONFIG == "LPRDI" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=-3
		echo "qu : config is LPRDI"
	fi

	if [ $CURR_CONFIG == "LPIRDM" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=0
		echo "qu : config is LPIRDM"
	fi

	if [ $CURR_CONFIG == "LPRDIM" ]; then
		CPU_NODE=0
		DATA_NODE=3
		PT_NODE=0
		echo "qu : config is LPRDIM"
	fi

	if [ $CURR_CONFIG == "RPRDI" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=-3
		echo "qu : config is RPRDI"
	fi

	if [ $CURR_CONFIG == "RPRDIM" ]; then
		CPU_NODE=3
		DATA_NODE=0
		PT_NODE=0
		echo "qu : config is RPRDIM"
	fi

	if [ $CURR_CONFIG == "RPLDM" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=0
    echo "qu : config is RPLDM"
  fi

  #CONFIGS="RPLDI RPLDIM"
  if [ $CURR_CONFIG == "RPLDI" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=-3
		echo "qu : config is RPLDI"
  fi
	
	if [ $CURR_CONFIG == "RPLDIM" ]; then
    CPU_NODE=3
    DATA_NODE=3
    PT_NODE=0
    echo "qu : config is RPLDIM"
  fi

	# --- setup interference node
	INT_NODE=0
        if [ $CURR_CONFIG == "LPRDI" ] || [ $CURR_CONFIG == "RPLDI" ] || [ $CURR_CONFIG == "LPRDI" ] || [ $CURR_CONFIG == "RPRDI" ]; then
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
	BENCHPATH="memtier_benchmark"
	PERF=$ROOT"/bin/perf"
	INT_BIN=$ROOT"/bin/bench_stream"
	NUMACTL=$ROOT"/bin/numactl"
        ICOLLECTOR=$ROOT"/bin/icollector"
        #if [ ! -e $BENCHPATH ]; then
        #    echo "Benchmark binary is missing"
        #    exit
        #fi
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
        if [[ $CONFIG == "RPILDM" ]] || [[ $FIRST_CHAR == "T" ]]; then
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
	OUTFILE_UNCORE=$RUNDIR/perflog_uncore-$BENCHMARK-$(hostname)-$CONFIG.dat
	OUTFILE=$RUNDIR/perflog-$BENCHMARK-$(hostname)-$CONFIG.dat
}

set_system_configs()
{
        CURR_CONFIG=$1
        FIRST_CHAR=${CURR_CONFIG:0:1}
        thp="never"
        if [[ $FIRST_CHAR == "T" ]]; then
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
        echo 0 | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting AutoNUMA to: 0"
                exit
        fi
        #echo $PT_NODE | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
        # if [ $? -ne 0 ]; then
        #         echo "ERROR setting pgtable allocation to node: $PT_NODE"
        #         exit
        # fi
         
	# --- check page table replication
        LAST_CHAR="${CURR_CONFIG: -1}"
        if [[ $LAST_CHAR == "M" ]]; then
                echo "M set page-table to node:"
                echo $PT_NODE | sudo tee /proc/sys/kernel/pgtable_replication
                if [ $? -ne 0 ]; then
                        echo "ERROR setting pgtable_replication to $0"
                        exit
                fi
                # --- drain first then reserve
                echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
                if [ $? -ne 0 ]; then
                        echo "ERROR setting pgtable_replication_cache to $0"
                        exit
                fi
                echo $NR_PTCACHE_PAGES | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
                if [ $? -ne 0 ]; then
                        echo "ERROR setting pgtable_replication_cache to $NR_PTCACHE_PAGES"
                        exit
                fi
        else
		#CMD_PREFIX+=" --pgtablerepl=$NODE_MAX "
                # --- enable default page table allocation
                echo "set page-table to node:"
                echo $PT_NODE | sudo tee /proc/sys/kernel/pgtable_replication
                if [ $? -ne 0 ]; then
                        echo "ERROR setting pgtable_replication to -1"
                        exit
                fi
                # --- drain page table cache
                echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
                if [ $? -ne 0 ]; then
                        echo "ERROR setting pgtable_replication to 0"
                        exit
                fi
        fi
}

launch_interference()
{
	CURR_CONFIG=$1
        LAST_CHAR=${CURR_CONFIG: -1}
        if [[ $LAST_CHAR == "M" ]]; then
            CURR_CONFIG=${CURR_CONFIG::-1}
        fi
	FIRST_CHAR=${CURR_CONFIG:0:1}
	if [[ $FIRST_CHAR == "T" ]]; then
		CURR_CONFIG=${CURR_CONFIG:1}
	fi
        Interference_PID=0
	if [[ $CURR_CONFIG == "RPLDI" ]] || [[ $CURR_CONFIG == "RPILD" ]] || [[ $CURR_CONFIG == "RPIRDI" ]] || [ $CURR_CONFIG == "RPIRD" ] || [ $CURR_CONFIG == "RPRDI" ] || [ $CURR_CONFIG == "LPRDI" ] || [ $CURR_CONFIG == "LPIRD" ] || [ $CURR_CONFIG == "LPLDI" ]; then
		$NUMACTL -c $INT_NODE -m $INT_NODE $INT_BIN > /dev/null 2>&1 &
                Interference_PID=$!
	        echo "Interference_PID : $Interference_PID"
                if [ $? -ne 0 ]; then
			echo "Failure launching interference."
			exit
		fi
	fi
}

launch_benchmark_config()
{
	# --- clean up exisiting state/processes
	#rm /tmp/alloctest-bench.ready &>/dev/null
	#rm /tmp/alloctest-bench.done &> /dev/null
	killall bench_stream &>/dev/null

    CMD_PREFIX=$NUMACTL
    CMD_PREFIX+=" -m $DATA_NODE -c $CPU_NODE "
    LAST_CHAR=${CONFIG: -1}
    # obtain the number of available nodes
#     NODESTR=$(numactl --hardware | grep available)
#     NODE_MAX=$(echo ${NODESTR##*: } | cut -d " " -f 1)
#     NODE_MAX=`expr $NODE_MAX - 1`
    if [[ $LAST_CHAR == "M" ]]; then
        CMD_PREFIX+=" --pgtablerepl=3"
    fi
    echo "CMD_PREFIX=$CMD_PREFIX"
    
    
    REDIS_PID=0
    if [[ $BENCHMARK == "memcache" ]];then
        # sudo $CMD_PREFIX memcached -d -m 62768 -p 6379 -t 24 -u root
        sudo $CMD_PREFIX memcached -d -m 62768 -p 6379 -u root
        REDIS_PID=$(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
	echo "memcached pid is $REDIS_PID"
        DATA_LOAD=$Memcached_DATA_LOAD
        BENCH_ARGS=$Memcached_BENCH_ARGS
    elif [[ $BENCHMARK == "kdb" ]];then
        sudo $CMD_PREFIX /home/huawei/wasp/KeyDB-main/src/keydb-server /home/huawei/wasp/KeyDB-main/src/keydb.conf
        REDIS_PID=$(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
        echo "keydb pid is $REDIS_PID"
    elif [[ $BENCHMARK == "redi" ]];then
        sudo $CMD_PREFIX redis-server redis.conf
        REDIS_PID=$(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
	echo "redis pid is $REDIS_PID"
    fi  

    echo "DATA_LOAD is $DATA_LOAD , BENCH_ARGS is $BENCH_ARGS"    
    
        #sudo $CMD_PREFIX $dir/redis02/redis-server $dir/redis02/redis.conf
        #sudo $CMD_PREFIX $dir/redis03/redis-server $dir/redis03/redis.conf
        #sudo $CMD_PREFIX $dir/redis04/redis-server $dir/redis04/redis.conf
        #sudo $CMD_PREFIX $dir/redis05/redis-server $dir/redis05/redis.conf
        #sudo $CMD_PREFIX $dir/redis06/redis-server $dir/redis06/redis.conf
	#sudo $CMD_PREFIX $dir/redis07/redis-server $dir/redis07/redis.conf
        #sudo $CMD_PREFIX $dir/redis08/redis-server $dir/redis08/redis.conf
	#sudo $CMD_PREFIX $dir/redis09/redis-server $dir/redis09/redis.conf
        #sudo $CMD_PREFIX $dir/redis10/redis-server $dir/redis10/redis.conf
        #sudo $CMD_PREFIX $dir/redis11/redis-server $dir/redis11/redis.conf
        #sudo $CMD_PREFIX $dir/redis12/redis-server $dir/redis12/redis.conf
        #sudo $CMD_PREFIX $dir/redis13/redis-server $dir/redis13/redis.conf
        #sudo $CMD_PREFIX $dir/redis14/redis-server $dir/redis14/redis.conf
        #sudo $CMD_PREFIX $dir/redis15/redis-server $dir/redis15/redis.conf
	#sudo $CMD_PREFIX $dir/redis16/redis-server $dir/redis16/redis.conf
        #redis-cli --cluster create --cluster-replicas 1 127.0.0.1:6379 127.0.0.1:6380 127.0.0.1:6381 127.0.0.1:6382 127.0.0.1:6383 127.0.0.1:6384
	
	#sudo $CMD_PREFIX redis-server redis.conf
	
        CMD_PREFIX=$NUMACTL
        CMD_PREFIX+=" -m $DATA_NODE -c $CPU_NODE "
	LAUNCH_CMD="$CMD_PREFIX $BENCHPATH  $DATA_LOAD"
#     LAUNCH_CMD="$BENCHPATH  $DATA_LOAD"
	echo $LAUNCH_CMD #>> $OUTFILE
    echo "load data......"
	$LAUNCH_CMD # > /dev/null 2>&1 &
	BENCHMARK_PID=$!

	#$DUMP $REDIS_PID 30 $CONFIG-pgtable-$TIME.log $CONFIG-pgtable-$TIME.csv > /dev/null 2>&1 &

	wait $BENCHMARK_PID

        # CMD_PREFIX=$NUMACTL
        # CMD_PREFIX+=" -m 3 -c 3 "

        LAUNCH_CMD="$CMD_PREFIX $BENCHPATH -o $NAME.log $BENCH_ARGS"
	# LAUNCH_CMD="$BENCHPATH -o $NAME.log $BENCH_ARGS"
	echo $LAUNCH_CMD
    echo "begin read......"
	$LAUNCH_CMD > /dev/null 2>&1 &
	BENCHMARK_PID=$!
	echo "first benchmark : $BENCHMARK_PID"
	SECNODS=0

	launch_interference $CONFIG

    $ICOLLECTOR $REDIS_PID perf-$NAME.log > /dev/null 2>&1 &
    # $ICOLLECTOR -1 perf-$NAME.log > /dev/null 2>&1 &
    icollector_pid=$!
        
    $PERF stat -a -x, -o $OUTFILE_UNCORE --append -e $PERF_UNCORE_EVENTS &
    PERF_PID=$!
	#$PERF stat -x, -o $OUTFILE --append -e $PERF_EVENTS -p $REDIS_PID &
	#PERF_PID=$!

	echo -e "\e[0mWaiting for benchmark to be done"
	numactl --hardware
    wait $BENCHMARK_PID
    numactl --hardware
    kill $icollector_pid
	DURATION=$SECONDS

	sudo kill -9 $REDIS_PID
	kill -INT $PERF_PID &> /dev/null
	wait $PERF_PID
	wait $BENCHMARK_PID 2>/dev/null
	echo "Execution Time (seconds): $DURATION"
	echo $DURATION >> record.log
	echo "****success****" >> $OUTFILE
	echo "$BENCHMARK : $CONFIG completed."
      
	killall bench_stream &>/dev/null

	# sudo bash clear.sh > /dev/null 2>&1 &
    echo "start clean ......"
sync
sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

if [[ $BENCHMARK == "memcache" ]];then
    ps axu|grep memcached
    sudo kill -9 $(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
elif [[ $BENCHMARK == "kdb" ]];then
    ps axu|grep keydb-server
    sudo kill -9 $(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
elif [[ $BENCHMARK == "redi" ]];then
    ps axu|grep redis
    sudo kill -9 $(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
fi
# sudo kill $memcached_pid01
echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
echo "wait  20s ......"
sleep 20
if [[ $BENCHMARK == "memcache" ]];then
    ps axu|grep memcached
elif [[ $BENCHMARK == "kdb" ]];then
    ps axu|grep keydb-server
elif [[ $BENCHMARK == "redi" ]];then
    ps axu|grep redis
fi
numactl --hardware
echo "clean  end......"


}


# validate_benchmark_config $BENCHMARK $CONFIG
#prepare_benchmark_name $BENCHMARK
prepare_basic_config_params $CONFIG
prepare_all_pathnames
#prepare_datasets $BENCHMARK
set_system_configs $CONFIG

# --- finally, launch the job
launch_benchmark_config





# if [ $BENCHMARK == "memcache" ];then
#     echo "memcached"
#     if [ $LAST_CHAR == "M" ];then
#         echo "page copy"
#         sudo bash set_pt_cache.sh m
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 -r 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1  # IM
#             # /home/huawei/wasp/bin/numactl -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         else
#             /home/huawei/wasp/bin/numactl -r 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1   # FM
#             # /home/huawei/wasp/bin/numactl memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         fi
#     else
#         echo "normal"
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1  # I
#         else
#             /home/huawei/wasp/bin/numactl memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1   # F
#         fi
#     fi
# elif [ $BENCHMARK == "kdb" ];then
#     echo "keydb"
#     if [ $LAST_CHAR == "M" ];then
#         echo "page copy"
#         sudo bash set_pt_cache.sh m
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 -r 0-3 /home/cyp/KeyDB-main/src/keydb-server /home/cyp/KeyDB-main/src/keydb.conf > /dev/null 2>&1 # IM
#             # /home/huawei/wasp/bin/numactl -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         else
#             /home/huawei/wasp/bin/numactl -r 0-3 /home/cyp/KeyDB-main/src/keydb-server /home/cyp/KeyDB-main/src/keydb.conf > /dev/null 2>&1   # FM
#             # /home/huawei/wasp/bin/numactl memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         fi
#     else
#         echo "normal"
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 /home/cyp/KeyDB-main/src/keydb-server /home/cyp/KeyDB-main/src/keydb.conf > /dev/null 2>&1 # I
#         else
#             /home/huawei/wasp/bin/numactl /home/cyp/KeyDB-main/src/keydb-server /home/cyp/KeyDB-main/src/keydb.conf > /dev/null 2>&1   # F
#         fi
#     fi
# elif [ $BENCHMARK == "redi" ];then
#     echo "redis"
#     if [ $LAST_CHAR == "M" ];then
#         echo "page copy"
#         sudo bash set_pt_cache.sh m
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 -r 0-3 redis-server redis.conf > /dev/null 2>&1 # IM
#             # /home/huawei/wasp/bin/numactl -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         else
#             /home/huawei/wasp/bin/numactl -r 0-3 redis-server redis.conf > /dev/null 2>&1   # FM
#             # /home/huawei/wasp/bin/numactl memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
#         fi
#     else
#         echo "normal"
#         if [ $FIRST_CHAR == "I" ];then
#             /home/huawei/wasp/bin/numactl -i 0-3 redis-server redis.conf > /dev/null 2>&1 # I
#         else
#             /home/huawei/wasp/bin/numactl redis-server redis.conf > /dev/null 2>&1   # F
#         fi
#     fi
# fi


# #redis-server redis.conf > /dev/null 2>&1 &


# count=500
# while [ $count -ne 0 ]
# do
#     count=`expr $count - 1`
# done    

# KEY=$2
# DATA_SIZE=$3
# REQUEST=`expr $KEY / 100`

# echo "the config is $CURR_CONFIG-$KEY-$REQUEST-$DATA_SIZE"

# if [ $BENCHMARK == "memcache" ];then
#     arr=$(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#     memcached_pid01=$(echo $arr | cut -d " " -f 1)
#     #redis_pid02=$(echo $arr | cut -d " " -f 2)

#     echo "first memcached-server is $memcached_pid01"
#     #echo "second redis-server is $redis_pid02"

#     memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     benchmark_pid_1=$!

#     #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     #benchmark_pid_2=$!

#     wait $benchmark_pid_1
# elif [ $BENCHMARK == "kdb" ];then
#     arr=$(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#     memcached_pid01=$(echo $arr | cut -d " " -f 1)
#     #redis_pid02=$(echo $arr | cut -d " " -f 2)

#     echo "first keydb-server is $memcached_pid01"
#     #echo "second redis-server is $redis_pid02"

#     memtier_benchmark -p 6379 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     benchmark_pid_1=$!

#     #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     #benchmark_pid_2=$!

#     wait $benchmark_pid_1

# elif [ $BENCHMARK == "redi" ];then
#     arr=$(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#     memcached_pid01=$(echo $arr | cut -d " " -f 1)
#     #redis_pid02=$(echo $arr | cut -d " " -f 2)

#     echo "first redis-server is $memcached_pid01"
#     #echo "second redis-server is $redis_pid02"

#     memtier_benchmark -p 6379 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     benchmark_pid_1=$!

#     #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
#     #benchmark_pid_2=$!

#     wait $benchmark_pid_1
# fi

# #wait $benchmark_pid_2

# #sudo bash record.sh $NAME-cpu.csv $pid > /dev/null 2>&1 &

# # for round in $(seq 1 3)
# for round in $(seq 1)
# do
#     echo "run round $round......"

#     NAME=$1-$round
#     echo "name $NAME......"
#     if [ $BENCHMARK == "memcache" ];then
#         memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 --test-time=1200 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
#     elif [ $BENCHMARK == "kdb" ];then
#         memtier_benchmark -p 6379 -t 20 -c 5 --test-time=1200 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
#     elif [ $BENCHMARK == "redi" ];then
#         memtier_benchmark -p 6379 -t 20 -c 5 --test-time=1200 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
#     fi
#     # memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 --test-time=10 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    
#     benchmark_pid_1=$!
    
#     echo "first benchmark : $benchmark_pid_1"

#     # $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
#     $ICOLLECTOR -1 perf-$NAME.log > /dev/null 2>&1 &
#     icollector_pid=$!
    
#     #$PT_DUMP $memcached_pid01 30 $NAME.log > /dev/null 2>&1 &

#     #memtier_benchmark -p 6380 -t 20 -c 5 --test-time=400 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o second-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
#     #benchmark_pid_2=$!

#     #echo "second benchmark : $benchmark_pid_2"

#     #$PT_DUMP $redis_pid02 30 $NAME.log > /dev/null 2>&1 &

#     wait $benchmark_pid_1 2>/dev/null
#     #wait $benchmark_pid_2 2>/dev/null
#     kill $icollector_pid

#     echo "round $round end"
# done

# echo "start clean ......"
# sync
# sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

# if [ $BENCHMARK == "memcache" ];then
#     ps axu|grep memcached
#     sudo kill -9 $(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
# elif [ $BENCHMARK == "kdb" ];then
#     ps axu|grep keydb-server
#     sudo kill -9 $(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
# elif [ $BENCHMARK == "redi" ];then
#     ps axu|grep redis
#     sudo kill -9 $(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
# fi
# # sudo kill $memcached_pid01
# echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
# echo "wait  20s ......"
# sleep 20
# if [ $BENCHMARK == "memcache" ];then
#     ps axu|grep memcached
# elif [ $BENCHMARK == "kdb" ];then
#     ps axu|grep keydb-server
# elif [ $BENCHMARK == "redi" ];then
#     ps axu|grep redis
# fi
# numactl --hardware
# echo "clean  end......"
# # sudo bash clear.sh
