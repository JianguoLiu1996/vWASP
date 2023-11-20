#!/bin/bash

CONFIG=$1

#NR_PTCACHE_PAGES=1048576 # --- 4GB per node
#NR_PTCACHE_PAGES=524288 # --- 2GB per node
# NR_PTCACHE_PAGES=262144 # --- 1GB per node
NR_PTCACHE_PAGES=1100000

test_and_set_configs() 
{ 
        CURR_CONFIG=$1
        FIRST_CHAR=${CURR_CONFIG:0:1}
        # --- check page table replication
        LAST_CHAR="${CURR_CONFIG: -1}"
	# echo "always" | sudo tee /sys/kernel/mm/transparent_hugepage/enabled > /dev/null
	# if [ $? -ne 0 ]; then
        #         echo "ERROR setting thp"
        #         exit
        # fi
	# echo "always" | sudo tee /sys/kernel/mm/transparent_hugepage/defrag > /dev/null
	# if [ $? -ne 0 ]; then
        #         echo "ERROR setting thp"
        #         exit
        # fi
	# echo "0" | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
	# if [ $? -ne 0 ]; then
        #         echo "ERROR setting AutoNUMA to: 0"
        #         exit
        # fi
        
	#sync
	#sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

	#sudo swapoff -a
	#sudo swapon -a

	# obtain the number of available nodes
        NODESTR=$(numactl --hardware | grep available)
        #echo $NODESTR
        NODE_MAX=$(echo ${NODESTR##*: } | cut -d " " -f 1)
        #echo $NODE_MAX
        NODE_MAX=`expr $NODE_MAX - 1` #等于3
        #echo $NODE_MAX
        #CMD_PREFIX=$NUMACTL

        if [ $FIRST_CHAR == "m" ]; then
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

}


# --- prepare the setup
test_and_set_configs $CONFIG
