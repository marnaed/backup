#!/bin/bash
# set -x


#workloads=/home/lupones/manager/experiments/individualAKC/spec-06-17.yaml
#inputdir=/home/lupones/manager/experiments/individualAKC
#outputdir=/home/lupones/manager/experiments/individualAKC/num-ways-tables

workloads=/home/lupones/manager/experiments/individualMisses/spec-06-17.yaml
inputdir=/home/lupones/manager/experiments/individualMisses
outputdir=/home/lupones/manager/experiments/individualMisses/box-plots


sudo python3 ./box-plot-graphs.py -w $workloads -id $inputdir -od $outputdir 
	






