#! /bin/bash
#在物理机上运行这个脚本，先关闭大页，因为虚拟机页表复制开启大页的还没有适配。
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled > /dev/null
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag > /dev/null
#关闭autoNUMA
echo 0 | sudo tee /proc/sys/kernel/numa_balancing > /dev/null
#关闭系统中之前的一些占内存的服务
sudo service mysql stop
sudo service redis-server stop

#具体测试的配置，可以根据测试不同而调整
#指定ept页表绑定在节点3上，使能ept固定功能。造成最坏的情况
#echo 3 | sudo tee /sys/kernel/mm/mitosis/current_ept_node > /dev/null
#echo 1 | sudo tee /sys/kernel/mm/mitosis/ept_migration > /dev/null
echo -1 | sudo tee /sys/kernel/mm/mitosis/current_ept_node > /dev/null
echo 0 | sudo tee /sys/kernel/mm/mitosis/ept_migration > /dev/null

#如果要打开页表复制，就打开这个
#echo 500000 | sudo tee /sys/kernel/mm/mitosis/ept_replication_cache > /dev/null
