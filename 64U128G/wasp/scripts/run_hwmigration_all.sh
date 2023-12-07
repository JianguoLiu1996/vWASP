#!/bin/bash

echo "**************************"
echo "***pgtablre test***"
echo "**************************"

ROOT=$(dirname `readlink -f "$0"`)

# BENCHMARKS="redi kdb memcache"
# BENCHMARKS="memcache redi kdb"
# BENCHMARKS="memcache"
BENCHMARKS="redi"
#BENCHMARKS="kdb"
# BENCHMARKS="my"
# CONFIGS="FM F I IM"
#CONFIGS="F FM"
# CONFIGS="I IM"
#CONFIGS="F"
#CONFIGS="F I"
# CONFIGS="LPLD RPILD RPILDM"
# CONFIGS="LPLD RPILD RPILDM RPLD RPLDM"
# CONFIGS="RPLDM RPILDM"
# CONFIGS="LPLD RPLD RPLDM"
# CONFIGS="LPLD LPLDM"
CONFIGS="LPLD"
# CONFIGS="RPILD"
# CONFIGS="RPILDM"
# CONFIGS="LPLD RPILD"
# CONFIGS="LPLD RPLDM"
# CONFIGS="LPLD RPRD RPRDM LPRD LPRDM"
# CONFIGS="LPRDI"
#CONFIGS="LPLD RPRD RPRDM RPIRD RPIRDM RPRDI RPRDIM LPRD LPRDM LPIRD LPIRDM LPRDI LPRDIM RPLD RPLDM RPILD RPILDM RPLDI RPLDIM"
# CONFIGS="RPRD RPRDM RPIRD RPIRDM RPRDI RPRDIM RPLD RPLDM RPILD RPILDM RPLDI RPLDIM"
# CONFIGS="RPIRDM"
# CONFIGS="TLPLD TRPILD TRPILDM"

# for round in $(seq 1 2)
#for round in $(seq 1 3)
for round in $(seq 1)
do
	echo "$BENCHMARKS run round $round......"
	for bench in $BENCHMARKS; do
		for config in $CONFIGS; do
			echo "***************$bench : $config***************"
			# bash $ROOT/run_hwmigration_one.sh $bench-$config-$round 800000000 24 $config $bench
			# bash $ROOT/run_hwmigration_one.sh $bench-$config-$round 80000000 1024 $config $bench

			#redis
			# bash $ROOT/run_hwmigration_one.sh $bench-$config-$round 160000000 64 $config $bench 900
			# bash $ROOT/run_hwmigration_one.sh $bench-$config-$round 240000000 24 $config $bench 900
			bash $ROOT/run_hwmigration_one.sh $bench-$config-$round 40000000 24 $config $bench 900
		done
	done
done

echo "**************************"
echo "*** test  end ***"
echo "**************************"
