#!/bin/bash
# set -x

# $1 = experiment 
# E.g 170719

#workloads=/home/lupones/manager/experiments/$1/spec-06-17.yaml
workloads=/home/lupones/manager/experiments/$1/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1

sudo python3 ./interval-tables.py -w $workloads -id $inputdir -od $outputdir -p noPart
#sudo python3 ./interval-tables.py -w $workloads -id $inputdir -od $outputdir -p 1w -p 2w -p 3w -p 4w -p 5w -p 6w -p 7w -p 8w -p 9w -p 10w -p 11w -p 12w -p 13w -p 14w -p 15w -p 16w -p 17w -p 18w -p 19w -p 20w
