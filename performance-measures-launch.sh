#!/bin/bash

results_file="test5.csv"
results_file_parts="test5_parts.csv"
num_repeats=1 #5

files=(
    # "./assets/spider0.sumocfg"
    # "./assets/spider1.sumocfg"
    # "./assets/spider2.sumocfg"
    # generated with scripts/random-simulation.sh grid_large --grid --grid.number 100 --grid.length 20
    # about 1GB large, so gitignored
    # "./assets/grid_large.sumocfg" 
    "./assets/bologna-sim/osm.sumocfg"
)

threadNumbers=(
    # 1 # For some reason tinyxml is giving FILE_NOT_FOUND with the original .sumocfg files, investigate later
    2
    4
    $(( $(nproc) / 2 )) # num cores in testing machine
    $(nproc) # num logical cores in testing machine
)

echo "cfg,thread no.,time,part.time" > "testResults/$results_file"
echo "cfg,thread no.,part,part.time" > "testResults/$results_file_parts"

for cfg in "${files[@]}"; do
    for N in "${threadNumbers[@]}"; do
        for i in {1..$num_repeats}; do
            echo "Running with: -N $N -c $cfg"
            COMMD="python ./measure-performance.py -N $N -c $cfg --pin-to-cpu"
            echo "$COMMD"
            perf_res=$($COMMD)
            echo "Results: $perf_res"
            echo "$cfg,$N,$perf_res" >> "testResults/$results_file"

            for part_idx in $(seq 0 $(( $N - 1 ))); do
                part_duration=$(grep -m 1 'Duration:.*s$' "data/log$part_idx.txt" | awk '{print $2}' | sed 's/s//')
                echo "$cfg,$N,$part_idx,$part_duration" >> "testResults/$results_file_parts"
            done
        done
    done
done
