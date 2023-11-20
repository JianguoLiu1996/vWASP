#!/bin/bash

echo "**************************"
echo "***pgtablre test***"
echo "**************************"

ROOT=$(dirname `readlink -f "$0"`)

# BENCHMARKS="memcache redi kdb"
# BENCHMARKS="memcache"
BENCHMARKS="mysql"
# BENCHMARKS="kdb"
#CONFIGS="F FM I IM"
# CONFIGS="F FM FW TF TFM TFW"
# CONFIGS="FM FW TF TFM TFW"
# CONFIGS="F FW TFM"
#CONFIGS="I IM"
#CONFIGS="TFW"
#CONFIGS="F I"
#CONFIGS="TF TFM TI TIM"
# CONFIGS="F"
# CONFIGS="FM FW"
# CONFIGS="F FM FW TF TFM TFW"
CONFIGS="F FM FW TF TFM TFW"
# CONFIGS="TF TFM TFW"
# CONFIGS="TFM"
# CONFIGS="TF TFW"
# CONFIGS="FW"
# for round in $(seq 1 3)
for round in $(seq 1)
do
	echo "$BENCHMARKS run round $round......"
	for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "***************$bench : $config***************"
			# bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 900000000 24 $config $bench 1200
			# bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 900000000 24 $config $bench 1200
			# bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 400000000 24 $config $bench 1200  #for memcached  TF
			#bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 80000000 1024 $config $bench 900
			# bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 80000000 1024 $config $bench 1200
			bash $ROOT/arm_run_mysql_one.sh $bench-$config-$round 70000000 1024 $config $bench 3600
		done
	done
done

echo "**************************"
echo "*** test  end ***"
echo "**************************"
