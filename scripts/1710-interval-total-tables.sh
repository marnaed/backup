#!/bin/bash
# set -x

# $1 = experiment 
# E.g 170719

workloads=/home/lupones/manager/experiments/$1/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1

sudo python3 ./1710-interval-total-tables.py -w $workloads -id $inputdir -od $outputdir -p linux -p criticalAlone
