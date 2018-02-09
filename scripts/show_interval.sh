#!/bin/bash
# set -x


workloads=/home/lupones/manager/experiments/$1v2/workloads$1.yaml
inputdir=/home/lupones/manager/experiments/$1v2
outputdir=/home/lupones/manager/experiments/$1v2

#sudo python3 /home/lupones/manager/scripts/show_interval.py -w $workloads -id $inputdir -od $outputdir -p criticalAloneMAD -p criticalAloneMEAN -p criticalAloneMEANTasks -p criticalAloneMEANTasks2std -p linux -p linux-fixed-allocation
#sudo python3 /home/lupones/manager/scripts/show_ipc.py -w $workloads -id $inputdir -od $outputdir -p criticalAloneMAD -p criticalAloneMEAN -p linux -p linux-fixed-allocation

python3 /home/lupones/manager/scripts/show_interval.py -w $workloads -id $inputdir -od $outputdir -p criticalAloneMEAN -p linux




