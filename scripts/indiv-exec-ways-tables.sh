#!/bin/bash
# set -x


#workloads=/home/lupones/manager/experiments/individualAKC/spec-06-17.yaml
#inputdir=/home/lupones/manager/experiments/individualAKC
#outputdir=/home/lupones/manager/experiments/individualAKC/num-ways-tables

workloads=/home/lupones/manager/experiments/individualPrefetch/spec-06-17.yaml
inputdir=/home/lupones/manager/experiments/individualPrefetch
outputdir=/home/lupones/manager/experiments/individualPrefetch/num-ways-tables


sudo python3 ./indiv-exec-ways-tables.py -w $workloads -id $inputdir -od $outputdir 
	






