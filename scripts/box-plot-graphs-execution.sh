#!/bin/bash
# set -x


#workloads=/home/lupones/manager/experiments/individualAKC/spec-06-17.yaml
#inputdir=/home/lupones/manager/experiments/individualAKC
#outputdir=/home/lupones/manager/experiments/individualAKC/num-ways-tables

workloads=/home/lupones/manager/experiments/$1/$2.yaml
inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1


sudo python3 ./box-plot-graphs-execution.py -w $workloads -id $inputdir -od $outputdir -p noPart
	






