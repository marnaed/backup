#!/bin/bash

# $1 = experiment (e.g. 170919)

inputdir=/home/lupones/manager/experiments/$1/criticalAware/log
outputdir=/home/lupones/manager/experiments/$1/criticalAware/states/

mkdir $outputdir

for filename in $inputdir/*.log; do
  
	# list of states from log file
	listStates=$(cat $filename | grep -E 'Current state')

	# create new file
    fn=$(echo $filename | awk -F'/' '{print $9}')
	fn=${fn::-4}
    fn=$(echo $outputdir$fn".states")
	echo $fn
    touch $fn

	while IFS= read -r line
	do
   		echo "$line" | tail -c 3 >> $fn 
	done < <(printf '%s\n' "$listStates")
done 


