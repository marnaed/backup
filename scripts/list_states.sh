#!/bin/bash

# $1 = experiment (e.g. 170919)
# $2 = policy name (e.g. criticalAware)

inputdir=/home/lupones/manager/experiments/$1/$2/log
outputdir=/home/lupones/manager/experiments/$1/$2/states/


mkdir $outputdir

for filename in $inputdir/*.log; do
    
    echo $filename
  
    touch aux.dat
    cat $filename | grep -E 'Current state' > aux.dat

    fn=$(echo $filename | awk -F'/' '{print $9}')
	fn=${fn::-4}
    fn=$(echo $outputdir$fn".states")
    

    echo $fn
    touch $fn

    while read l; do
        echo $l | tail -c 3 >> $fn
    done <aux.dat

    rm aux.dat


done 


