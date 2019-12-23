#!/bin/bash

stellar=$(readlink -f ../src/stellar-core)
echo stellar:$stellar

cd ../message-broker-build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)
cd ../run-stellar

n_nodes=2
debug_node=1

# run processes and store pids in array
for i in $(seq 1 $n_nodes); do
    cd node$i
    rm -r buckets
    rm stellar.db*
    gnome-terminal --wait --window -- $stellar new-db &
    pids[$i]=$!
    echo "create $!"
    cd ..
done

# wait for all pids
echo PIDS ${pids[*]}
for pid in ${pids[*]}; do
    echo "waiting $pid"
    wait $pid
done

let ind_begin=1+$debug_node
for i in $(seq $ind_begin $n_nodes); do
    cd node$i
    gnome-terminal --window --tab -e $stellar' run' & 
    cd ..
done