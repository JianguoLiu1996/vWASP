#! /bin/bash
echo 1
SCRIPTROOT=$(dirname `readlink -f "$0"`)
ROOT=$(dirname `readlink -f "$SCRIPTROOT"`)

echo $SCRIPTROOT
echo $ROOT

BINDIRECTORY=$(readlink $ROOT/bin)
BINDIR=$(basename $BINDIRECTORY)

echo $BINDIRECTORY
echo $BINDIR

HST=hostname

echo $HST
