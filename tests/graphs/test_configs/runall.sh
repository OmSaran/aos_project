#!/bin/bash

rm -rf /dev/shm/rerun
mkdir /dev/shm/rerun
mkdir op

RESULTS_DIR=/home/cc/
TARGET_DIR=/home/cc/
BIN_DIR=/home/cc/aos_project/build


for i in `ls *_config.json`
do
python run_test.py -f $i -r $RESULTS_DIR -t $TARGET_DIR --bin $BIN_DIR
if [ $? -ne 0 ] 
then 
  echo "Failed"
  exit 1 
fi
done
