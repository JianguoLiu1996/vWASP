#!/bin/bash

echo "**************************"
echo "***pgtablre test***"
echo "**************************"

ROOT=$(dirname `readlink -f "$0"`)

# BENCHMARKS="memcache redi kdb"
# BENCHMARKS="memcache"
BENCHMARKS="redi"
# BENCHMARKS="kdb"
#CONFIGS="F FM I IM"
# CONFIGS="F FM FW TF TFM TFW"
# CONFIGS="F FM FW"
#CONFIGS="I IM"
CONFIGS="TFW"
# CONFIGS="TF TFM"
#CONFIGS="TF TFM TI TIM"
# CONFIGS="F"
# CONFIGS="FW TFW"

# for round in $(seq 1 3)
for round in $(seq 1)
do
	echo "$BENCHMARKS run round $round......"
	for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "***************$bench : $config***************"
			# bash $ROOT/my-test-auto.sh $bench-$config-$round 800000000 24 $config $bench 1200
			# bash $ROOT/my-test-auto.sh $bench-$config-$round 80000000 1024 $config $bench 900
			# bash $ROOT/my-test-auto.sh $bench-$config-$round 80000000 1024 $config $bench 1200
			# bash $ROOT/my-test-auto.sh $bench-$config-$round 75000000 1024 $config $bench 1200
			bash $ROOT/my-test-auto.sh $bench-$config-$round 70000000 1024 $config $bench 900
		done
	done
done

echo "**************************"
echo "*** test  end ***"
echo "**************************"
