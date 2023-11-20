#!/bin/bash
#function stopSoftware(){
#	#sudo kill -9 $(ps aux | grep 'memtier_benchmark' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#	#sudo kill -9 $(ps aux | grep 'redis' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#	#sudo kill -9 $(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#	#sudo kill -9 $(ps aux | grep 'keydb' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
#	#killall bench_stream &>/dev/null
#	#sudo page-table-dump 1 1 why.log why.csv
#}

function clearMemory(){
	sync
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

function clearPagetableReplicationCache(){
	echo -1 | sudo tee /proc/sys/kernel/pgtable_replication > /dev/null
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

	echo "Sign: success clear page table replication cache!"
}

function disableSWAP(){
	sudo swapoff -a
	echo "Sign: success disable SWAP!"
}

function disableAutoNUMA(){
        # disable auto numa
        echo 0 | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
        if [ $? -ne 0 ]; then
                echo "ERROR setting AutoNUMA to: 0"
                exit
        fi

        cat /proc/sys/kernel/numa_balancing
        echo "SIGN:success disable Auto NUMA"
}


function disableTHP(){
        sudo echo never > /sys/kernel/mm/transparent_hugepage/enabled
        cat /sys/kernel/mm/transparent_hugepage/enabled

	sudo echo never > /sys/kernel/mm/transparent_hugepage/defrag
	cat /sys/kernel/mm/transparent_hugepage/defrag
}
#stopSoftware
disableTHP
disableAutoNUMA
disableSWAP
#clearPagetableReplicationCache
clearMemory
