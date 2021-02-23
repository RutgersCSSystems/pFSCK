#!/bin/bash

# Takes a given image and corrupts a set amount of bytes
#
#
# Usage:
#   ./create_corrupt.sh <base image> <# bytes to corrupt>

BUILD_PATH=$FSCKEXEPATH
E2FUZZ=$BUILD_PATH/misc/e2fuzz

REF_PATH=$1
REF_NAME=`echo $REF_PATH | cut -f 1 -d '.'`

CORR_PATH=$REF_NAME-corrupted-$2.img
FUZZ_LOG=$REF_NAME-fuzz.log

echo "Using the following reference file: $REF_PATH"
echo "Corrupting image to: $CORR_PATH"
echo "Recording fuzz log to: $FUZZ_LOG"
echo "Corrupting $2 bytes"

set -x

echo "removing any existing copies"
rm $CORR_PATH > /dev/null

cp $REF_PATH $CORR_PATH

$E2FUZZ -v -b $2 $CORR_PATH > $FUZZ_LOG 


