#!/bin/bash
# set -x

# $1 = experiment 
# E.g 170719

inputdir=/home/lupones/manager/experiments/individual/20w/resultTables/
outputdir=/home/lupones/manager/experiments/individual/20w/scatter-20w/
sudo python3 ./scatter-appType-graph.py -fn critical.yaml -fn noncritical.yaml -fn medium.yaml -fn problematic.yaml -id $inputdir -od $outputdir 

#inputdir=/home/lupones/manager/experiments/190510/test/resultTables/
#outputdir=/home/lupones/manager/experiments/190510/test/scatter/
#sudo python3 ./scatter-graph.py -fn /home/lupones/manager/experiments/190510/w.yaml -id $inputdir -od $outputdir 
