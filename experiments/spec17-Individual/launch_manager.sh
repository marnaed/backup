# !/bin/bash

for i in ExecutionStalls L1Bound L2Bound L3Bound; do
	cd $i

	sudo rm -r run
	#time sudo python3 ~/manager/scripts/aggdata.py -i data -o data-agg -w ./spec-06-17.yaml -n $i --alone 120
    sudo bash ~/manager/scripts/launch.bash ./spec-06-17.yaml
    
    cd ..

done
