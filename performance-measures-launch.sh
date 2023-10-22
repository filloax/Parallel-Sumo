#!/bin/bash

results_file="test4.csv"

files=(
    "./assets/spider0.sumocfg"
    "./assets/spider1.sumocfg"
    "./assets/spider2.sumocfg"
    # generated with scripts/random-simulation.sh grid_large --grid --grid.number 100 --grid.length 20
    # about 1GB large, so gitignored
    "./assets/grid_large.sumocfg" 
)

threadNumbers=(
    1
    4
    $(( $(nproc) / 2 )) # num cores in testing machine
    $(nproc) # num logical cores in testing machine
)

echo "cfg,thread no.,time,part.time" > "testResults/$results_file"

for cfg in "${files[@]}"; do
    for N in "${threadNumbers[@]}"; do
        for i in 1 .. 5; do
            echo "Running with: -N $N -c $cfg"
            echo "python ./measure-performance.py -N $N -c $cfg"
            perf_res=$(python ./measure-performance.py -N $N -c $cfg)
            echo "Results: $perf_res"
            echo "$cfg,$N,$perf_res" >> "testResults/$results_file"
        done
    done
done
