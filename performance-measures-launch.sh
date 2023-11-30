#!/bin/bash

results_file="test8.csv"
results_file_parts="test8_parts.csv"
num_repeats=3

files=(
    "./assets/test/spider0.sumocfg"
    "./assets/test/spider0_lowtraffic.sumocfg"
    "./assets/test/spider1.sumocfg"
    "./assets/test/spider1_lowtraffic.sumocfg"
    "./assets/test/spider2.sumocfg"
    "./assets/test/spider2_lowtraffic.sumocfg"
    # generated with scripts/random-simulation.sh grid_large --grid --grid.number 100 --grid.length 20
    # about 1GB large, so gitignored
    "./assets/test/grid_large.sumocfg" 
    "./assets/test/grid_large_lowtraffic.sumocfg" 
    "./assets/test/bologna-sim/osm_lowtraffic.sumocfg"
    "./assets/test/bologna-sim/osm.sumocfg"
    "./assets/test/bologna-metropolitan-area/osm_lowtraffic.sumocfg"
    "./assets/test/bologna-metropolitan-area/osm.sumocfg"
)

threadNumbers=(
    1 # For some reason tinyxml is giving FILE_NOT_FOUND with the original .sumocfg files, investigate later
    2
    4
    $(( $(nproc) / 2 )) # num cores in testing machine
    $(nproc) # num logical cores in testing machine
)

echo "cfg,thread no.,time,part.time" > "testResults/$results_file"
echo "cfg,thread no.,part,part.time,vehicles" > "testResults/$results_file_parts"

TOT=0

for cfg in "${files[@]}"; do
    for N in "${threadNumbers[@]}"; do
        TOT=$(($TOT + 1))
    done
done

PROG=0
for cfg in "${files[@]}"; do
    for N in "${threadNumbers[@]}"; do
        PROG=$(($PROG + 1))
        for i in $(seq 1 $num_repeats); do
            echo "Running with: -N $N -c $cfg [R$i/$num_repeats] [$PROG/$TOT]"
            COMMD="python ./measure-performance.py -N $N -c $cfg --pin-to-cpu"
            echo "$COMMD"
            perf_res=$($COMMD)
            echo "Results: $perf_res"
            echo "$cfg,$N,$perf_res" >> "testResults/$results_file"

            for part_idx in $(seq 0 $(( $N - 1 ))); do
                part_duration=$(grep -m 1 'Duration:.*s$' "data/part$part_idx""_log.txt" | awk '{print $2}' | sed 's/s//')
                tot_vehicles=$(grep -m 1 'Inserted:' "data/part$part_idx""_log.txt" | awk '{print $2}' )
                echo "$cfg,$N,$part_idx,$part_duration,$tot_vehicles" >> "testResults/$results_file_parts"
            done
        done
    done
done
