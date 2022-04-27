#!/bin/bash

# Must run w/ root-privileges


if [ $# -lt 2 ]; then
    echo "Usage: sudo ./benchmark.sh <size-in-MB> <cp-executable> [opts]" >&2
    exit 0
fi

# if [ ! -x "$2" ]; then
#     echo "Program $2 not found" >&2
#     exit 1
# fi

FILENAME="_test"
filecopy="_testcopy"

if [ ! -f $FILENAME ]
then
    echo "Benchmark file doesn't exist, creating one"
    dd if=/dev/urandom of=$FILENAME bs=1M count=$1 status=none
else
    FILESIZE=$(stat -c%s "$FILENAME")
    EXPECTED=`expr $1 \* 1024 \* 1024`
    if [ $FILESIZE -ne $EXPECTED ] 
    then
        echo "Benchmark file has different size, recreating one"
        dd if=/dev/urandom of=$FILENAME bs=1M count=$1 status=none
    else
        echo "Benchmark file exists"
    fi
fi

if [ -f $filecopy ]; then
    echo "Deleting existing file copy"
    rm $filecopy
fi

echo "Clearing buffer cache"
sync && echo 3 > /proc/sys/vm/drop_caches

if [ $# -ge 3 ]
then
    time "$2" ${@:3} $FILENAME $filecopy
else
    time "$2" $FILENAME $filecopy
fi