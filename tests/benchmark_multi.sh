#!/bin/bash

# Must run w/ root-privileges

if [ $# -lt 3 ]; then
    echo "Usage: sudo ./benchmark.sh <size-in-MB> <num-files> <cp-executable> [opts]" >&2
    exit 0
fi

if [ ! -x "$3" ]; then
    echo "Program $3 not found" >&2
    exit 1
fi

FILEDIR="rootdir/"
FILEDIRCOPY="rcopy/"
EFILESIZE=`expr $1 \* 1024 \* 1024`
if [ ! -d $FILEDIR ]
then
    echo "Benchmark folder doesn't exist, creating one"
    python3 generator.py -d 1 -b 0 -n $2 -l $EFILESIZE -m $EFILESIZE
else
    FILESIZE=$(stat -c%s "$FILEDIR/file0")
    if [ $FILESIZE -ne $EFILESIZE ] 
    then
        echo "Benchmark files have different size, recreating benchmark folder"
        rm -r rootdir/
        python3 generator.py -d 1 -b 0 -n $2 -l $EFILESIZE -m $EFILESIZE
    else
        COUNT=$(find "$FILEDIR" -maxdepth 1 -type f | wc -l)
        if [ $COUNT -ne $2 ]
        then
            echo "Benchmark files have different number, recreating benchmark folder"
            rm -r rootdir/
            python3 generator.py -d 1 -b 0 -n $2 -l $EFILESIZE -m $EFILESIZE
        else
            echo "Benchmark folder exists"
        fi
    fi
fi

if [ -d $FILEDIRCOPY ]; then
    echo "Deleting existing file copy dir"
    rm -r $FILEDIRCOPY
fi

echo "Clearing buffer cache"
sync
echo 3 > /proc/sys/vm/drop_caches

if [ $# -gt 3 ]
then
    time "$3" -r ${@:4} $FILEDIR $FILEDIRCOPY
else
    time "$3" -r $FILEDIR $FILEDIRCOPY
fi