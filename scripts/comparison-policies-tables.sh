#!/bin/bash
# set -x


workloads=/home/lupones/manager/experiments/$1/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/outputCSVfiles/$1
outputdir=/home/lupones/manager/experiments/outputCSVfiles/$1

sudo python3 ./comparison-policies-tables.py -w $workloads -id $inputdir -od $outputdir -p criticalAware -p noPart -p dunn 
	






