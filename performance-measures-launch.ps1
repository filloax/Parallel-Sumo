$files = @(
    "./assets/spider0.sumocfg",
    "./assets/spider1.sumocfg",
    "./assets/spider2.sumocfg",
    # generated with \scripts\Random-Simulation.ps1 grid_large --grid --grid.number 300 --grid.length 20
    # about 1GB large, so gitignored
    "./assets/grid_large.sumocfg" 
)

$threadNumbers = @(
    1,
    4,
    6, # num cores
    12 # num logical cores
)

Write-Output "cfg,thread no.,duration,traci duration,realtime factor,UPS" > "testResults/test2.csv"

foreach ($cfg in $files) {
    foreach ($N in $threadNumbers) {
        Write-Host "Running with: -N $N -c $cfg"
        Write-Output "$cfg,$N,$(python .\measure-performance.py -N $N -c $cfg)" 2>err.txt >> "testResults/test2.csv"
    }
}