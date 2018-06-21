#!/bin/bash
# set -x


workloads=/home/lupones/manager/experiments/EUROPAR_Results/180215-XPL3/workloads180215-XPL3.yaml
inputdir=/home/lupones/manager/experiments/EUROPAR_Results/180215-XPL3
outputdir=/home/lupones/manager/experiments/TFG/aaa

sudo python3 ./show_names_tables.py -w $workloads -id $inputdir -od $outputdir -n power/energy-pkg/ -n power/energy-ram/ -n interval -n unfairness


