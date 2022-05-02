#!/bin/bash

rm -rf /dev/shm/rerun
mkdir /dev/shm/rerun
mkdir op

for i in `ls *_config.json`
do
DEBUG=1 python ../generic.py -f $i -r /home/cc/aos/aos_project/results_ssd -t /home/cc/aos/aos_project/tests/graphs/ssd_configs/runs > op/stdout_$i.out 2> op/stderr_$i.out
done
