#!/bin/bash

sync
sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

sudo kill -9 $(ps aux | grep 'memtier_benchmark' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
sudo kill -9 $(ps aux | grep 'redis' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
sudo kill -9 $(ps aux | grep 'memcached' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)
sudo kill -9 $(ps aux | grep 'keydb' | grep -v grep | tr -s ' '| cut -d ' ' -f 2)

echo -1 | sudo tee /proc/sys/kernel/pgtable_replication_cache > /dev/null

killall bench_stream &>/dev/null
#sudo page-table-dump 1 1 why.log why.csv

#sudo swapoff -a
#sudo swapon -a

