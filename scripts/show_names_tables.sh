#!/bin/bash
# set -x


workloads=/home/lupones/manager/experiments/$1/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1

sudo python3 ./show_names_tables.py -w $workloads -id $inputdir -od $outputdir -n power/energy-pkg/ -n power/energy-ram/ -n interval -n unfairness -n ipc


