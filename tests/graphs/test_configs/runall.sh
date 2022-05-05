#!/bin/bash

rm -rf /dev/shm/rerun
mkdir /dev/shm/rerun
mkdir op

# INSTRUCTIONS:
# 1. Add git rootdir to PYTHONPATH
# 2. Do not forget to modify -r -t and --bin arguments in the command below

for i in `ls *_config.json`
do
DEBUG=1 python generic.py -f $i -r /home/cc/aos_project/new_results_ssd_10k -t /home/cc/aos_project/tests/wdir --bin /home/cc/aos_project/build > op/stdout_$i.out 2> op/stderr_$i.out
done
