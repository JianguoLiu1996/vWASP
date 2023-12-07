#!/bin/bash

#PT_DUMP="/home/huawei/experiment/dumptest/bin/page-table-dump"
# #sudo bash set_pt_cache.sh m

# ICOLLECTOR="/home/cyp/icollector/icollector"

if [ $# -ne 6 ];then
    echo "input the name"
    exit
fi
SCRIPTS=$(readlink -f "`dirname $(readlink -f "$0")`")
ROOT="$(dirname "$SCRIPTS")"
NUMACTL=$ROOT"/bin/numactl"
ICOLLECTOR=$ROOT"/bin/icollector"
if [ ! -e $NUMACTL ]; then
    echo "numactl is missing"
    exit
fi
if [ ! -e $ICOLLECTOR ]; then
    echo "ICOLLECTOR is missing"
    exit
fi

#NR_PTCACHE_PAGES=1048576 # --- 4GB per node
#NR_PTCACHE_PAGES=524288 # --- 2GB per node
# NR_PTCACHE_PAGES=262144 # --- 1GB per node
NR_PTCACHE_PAGES=1100000

BENCHMARK=$5
TIME=$6
CURR_CONFIG=$4
FIRST_CHAR=${CURR_CONFIG:0:1}
# --- check page table replication
LAST_CHAR="${CURR_CONFIG: -1}"
thp="never"
if [ $FIRST_CHAR == "T" ]; then
    thp="always"
fi
#thp="always"
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
cat /sys/kernel/mm/transparent_hugepage/enabled

echo "0" | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
if [ $? -ne 0 ]; then
        echo "ERROR setting AutoNUMA to: 0"
        exit
fi
cat /proc/sys/kernel/numa_balancing

if [ $LAST_CHAR == "M" ]; then
        echo "use mitosis"
        echo 0 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
                #if [ $? -ne 0 ]; then
                #        echo "ERROR setting pgtable_replication to $0"
                #        exit
                #fi
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
		echo "enable replication"
elif [ $LAST_CHAR == "W" ]; then
        echo "use wasp"
        echo 0 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
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
        echo -1 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
                #if [ $? -ne 0 ]; then
                #        echo "ERROR setting pgtable_replication to -1"
                #        exit
                #fi
                # --- drain page table cache
        echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting pgtable_replication to 0"
                exit
        fi
		echo "default"
fi



if [ $BENCHMARK == "memcache" ];then
    echo "memcached"
    if [ $LAST_CHAR == "W" ] || [ $LAST_CHAR == "M" ];then
        echo "page copy mitosis"
        # #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 -r 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1  # IM
            # $NUMACTL -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        else
            $NUMACTL -r 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1   # FM
            # $NUMACTL memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        fi
    else
        echo "normal"
        #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1  # I
        else
            $NUMACTL memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1   # F
        fi
    fi
elif [ $BENCHMARK == "kdb" ];then
    echo "keydb"
    if [ $LAST_CHAR == "W" ] || [ $LAST_CHAR == "M" ];then
        echo "page copy mitosis"
        #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 -r 0-3 /home/huawei/wasp/KeyDB-main/src/keydb-server /home/huawei/wasp/KeyDB-main/src/keydb.conf > /dev/null 2>&1 # IM
            # $NUMACTL -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        else
            $NUMACTL -r 0-3 /home/huawei/wasp/KeyDB-main/src/keydb-server /home/huawei/wasp/KeyDB-main/src/keydb.conf > /dev/null 2>&1   # FM
            # $NUMACTL memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        fi
    else
        echo "normal"
        #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 /home/huawei/wasp/KeyDB-main/src/keydb-server /home/huawei/wasp/KeyDB-main/src/keydb.conf > /dev/null 2>&1 # I
        else
            $NUMACTL /home/huawei/wasp/KeyDB-main/src/keydb-server /home/huawei/wasp/KeyDB-main/src/keydb.conf > /dev/null 2>&1   # F
        fi
    fi
elif [ $BENCHMARK == "redi" ];then
    echo "redis"
    if [ $LAST_CHAR == "W" ] || [ $LAST_CHAR == "M" ];then
        echo "page copy mitosis"
        #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 -r 0-3 redis-server redis.conf > /dev/null 2>&1 # IM
            # $NUMACTL -i 0-3 memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        else
            $NUMACTL -r 0-3 redis-server redis.conf > /dev/null 2>&1   # FM
            # $NUMACTL memcached -d -m 102400 -p 6379 -t 96 -u root > /dev/null 2>&1
        fi
    else
        echo "normal"
        #sudo bash set_pt_cache.sh m
        if [ $FIRST_CHAR == "I" ];then
            $NUMACTL -i 0-3 redis-server redis.conf > /dev/null 2>&1 # I
        else
            $NUMACTL redis-server redis.conf > /dev/null 2>&1   # F
        fi
    fi
fi


#redis-server redis.conf > /dev/null 2>&1 &


count=500
while [ $count -ne 0 ]
do
    count=`expr $count - 1`
done    

KEY=$2
DATA_SIZE=$3
REQUEST=`expr $KEY / 100`

echo "the config is $CURR_CONFIG-$KEY-$REQUEST-$DATA_SIZE"

if [ $BENCHMARK == "memcache" ];then
    arr=$(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
    memcached_pid01=$(echo $arr | cut -d " " -f 1)
    #redis_pid02=$(echo $arr | cut -d " " -f 2)

    echo "first memcached-server is $memcached_pid01"
    #echo "second redis-server is $redis_pid02"
    # if [ $LAST_CHAR == "W" ];then
    #     echo "use ICOLLECTOR"
    #     $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
    #     icollector_pid=$!
    # fi
    memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    benchmark_pid_1=$!

    #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    #benchmark_pid_2=$!

    wait $benchmark_pid_1
elif [ $BENCHMARK == "kdb" ];then
    arr=$(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
    memcached_pid01=$(echo $arr | cut -d " " -f 1)
    #redis_pid02=$(echo $arr | cut -d " " -f 2)

    echo "first keydb-server is $memcached_pid01"
    # if [ $LAST_CHAR == "W" ];then
    #     echo "use ICOLLECTOR"
    #     $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
    #     icollector_pid=$!
    # fi
    #echo "second redis-server is $redis_pid02"
# $NUMACTL -m 3 -c 3 memtier_benchmark -p 6379 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    memtier_benchmark -p 6379 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    benchmark_pid_1=$!

    #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    #benchmark_pid_2=$!

    wait $benchmark_pid_1

elif [ $BENCHMARK == "redi" ];then
    arr=$(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
    memcached_pid01=$(echo $arr | cut -d " " -f 1)
    #redis_pid02=$(echo $arr | cut -d " " -f 2)

    echo "first redis-server is $memcached_pid01"
    #echo "second redis-server is $redis_pid02"
    # if [ $LAST_CHAR == "W" ];then
    #     echo "use ICOLLECTOR"
    #     $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
    #     icollector_pid=$!
    # fi

    $NUMACTL -m 3 -c 3 memtier_benchmark -p 6379 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    benchmark_pid_1=$!

    #memtier_benchmark -p 6380 -t 20 -c 5 -n $REQUEST -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=1:0 --key-pattern=P:P --pipeline=10000 --hide-histogram > /dev/null 2>&1 &
    #benchmark_pid_2=$!
    wait $benchmark_pid_1
fi

if [ $LAST_CHAR == "W" ];then
    echo "use ICOLLECTOR"
    $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
    icollector_pid=$!
    sleep 20
    echo "start test"
fi
#wait $benchmark_pid_2

#sudo bash record.sh $NAME-cpu.csv $pid > /dev/null 2>&1 &

# for round in $(seq 1 3)
for round in $(seq 1)
do
    echo "run round $round......"

    NAME=$1-$round
    # NAME2=$1-$round-$round
    echo "name $NAME......"
    if [ $BENCHMARK == "memcache" ];then
        # $NUMACTL -m 3 -c 3 memtier_benchmark -p 6379 -P memcache_text -t 60 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
        memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
        # memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME2-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    elif [ $BENCHMARK == "kdb" ];then
        $NUMACTL -m 3 -c 3 memtier_benchmark -p 6379 -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
        # memtier_benchmark -p 6379 -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    elif [ $BENCHMARK == "redi" ];then
        # $NUMACTL -m 3 -c 3 memtier_benchmark -p 6379 -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
        memtier_benchmark -p 6379 -t 20 -c 5 --test-time=$TIME -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    fi
    # memtier_benchmark -p 6379 -P memcache_text -t 20 -c 5 --test-time=10 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o first-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    
    benchmark_pid_1=$!
    
    echo "first benchmark : $benchmark_pid_1"

    # $ICOLLECTOR $memcached_pid01 perf-$NAME.log > /dev/null 2>&1 &
    # $ICOLLECTOR -1 perf-$NAME.log > /dev/null 2>&1 &
    # icollector_pid=$!
    
    #$PT_DUMP $memcached_pid01 30 $NAME.log > /dev/null 2>&1 &

    #memtier_benchmark -p 6380 -t 20 -c 5 --test-time=400 -R --randomize --distinct-client-seed -d $DATA_SIZE --key-maximum=$KEY --key-minimum=1 --ratio=0:1 --key-pattern=R:R -o second-$NAME-qps.log --hide-histogram --pipeline=10000 > /dev/null 2>&1 &
    #benchmark_pid_2=$!

    #echo "second benchmark : $benchmark_pid_2"

    #$PT_DUMP $redis_pid02 30 $NAME.log > /dev/null 2>&1 &

    wait $benchmark_pid_1 2>/dev/null
    #wait $benchmark_pid_2 2>/dev/null
    # kill $icollector_pid

    echo "round $round end"
done

echo "start clean ......"
sync
sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

if [ $LAST_CHAR == "W" ];then
    echo "kill ICOLLECTOR"
    kill -INT $icollector_pid &> /dev/null
    wait $icollector_pid
fi

if [ $BENCHMARK == "memcache" ];then
    ps axu|grep memcached
    sudo kill -9 $(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
elif [ $BENCHMARK == "kdb" ];then
    ps axu|grep keydb-server
    sudo kill -9 $(ps aux | grep 'keydb-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
elif [ $BENCHMARK == "redi" ];then
    ps axu|grep redis
    sudo kill -9 $(ps aux | grep 'redis-server' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
fi
# sudo kill $memcached_pid01
echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null
echo "wait  20s ......"
sleep 20
if [ $BENCHMARK == "memcache" ];then
    ps axu|grep memcached
elif [ $BENCHMARK == "kdb" ];then
    ps axu|grep keydb-server
elif [ $BENCHMARK == "redi" ];then
    ps axu|grep redis
fi
numactl --hardware
echo "clean  end......"
# sudo bash clear.sh
