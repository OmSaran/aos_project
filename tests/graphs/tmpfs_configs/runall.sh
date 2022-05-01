#!/bin/bash

rm -rf /dev/shm/rerun
mkdir /dev/shm/rerun
mkdir op

for i in `ls *_config.json`
do
DEBUG=1 python generic.py -f $i -r ../../new_results -t /dev/shm/rerun > op/stdout_$i.out 2> op/stderr_$i.out
done
