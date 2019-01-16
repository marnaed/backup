#!/bin/bash
# set -x


inputdir=/home/lupones/manager/experiments/$1
outputdir=/home/lupones/manager/experiments/$1/overhead/

sudo python3 ./overhead_calculation.py -id $inputdir -od $outputdir -p noPart -p criticalAware -p CAV2_Schwetman -p CAV2_Carling -p CAV2_q3 -p CAV2_mad -p CAV2_Turkey -p CAV2_3std -p CAV2_auto100 -p CAV2_auto75 -p CAV2_auto50 -p cad 

