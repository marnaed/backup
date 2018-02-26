#!/bin/bash
# set -x


workloads=/home/lupones/manager/experiments/$1/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1

sudo python3 /home/lupones/manager/scripts/show_ipc.py -w $workloads -id $inputdir -od $outputdir -p criticalAlone -p linux 
sudo python3 /home/lupones/manager/scripts/show_interval.py -w $workloads -id $inputdir -od $outputdir -p linux -p criticalAlone


