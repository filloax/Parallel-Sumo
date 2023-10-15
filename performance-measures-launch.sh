#!/bin/bash

files=(
    "./assets/spider0.sumocfg"
    "./assets/spider1.sumocfg"
    "./assets/spider2.sumocfg"
    # generated with scripts/Random-Simulation.ps1 grid_large --grid --grid.number 100 --grid.length 20
    # about 1GB large, so gitignored
    # "./assets/grid_large.sumocfg" 
)

threadNumbers=(
    1
    4
    6 # num cores in testing machine
    12 # num logical cores in testing machine
)

echo "cfg,thread no.,duration,traci duration,realtime factor,UPS" > "testResults/test3.csv"

for cfg in "${files[@]}"; do
    for N in "${threadNumbers[@]}"; do
        echo "Running with: -N $N -c $cfg"
        echo "python ./measure-performance.py -N $N -c $cfg"
        echo "$cfg,$N,$(python ./measure-performance.py -N $N -c $cfg)" >> "testResults/test3.csv"
    done
done
