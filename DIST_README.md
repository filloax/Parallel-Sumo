# Parallel-Sumo Tweaks

Tweaks to the Parallel-Sumo repo as part of a thesis work, to study possibilities of parallelization in SUMO.

Remember to set the SUMO_HOME environment variable to the directory containing SUMO!

Check the [original repository](https://github.com/filloax/Parallel-Sumo) for the full readme

## How to use

Launch the script `launch.sh` with **--help** as an argument to check all available options. The main args are **-c** to specify the .sumocfg file to run the simulation from, **-N** to specify the number of processes, and **--gui** if you want to see a GUI of the partition. You can specify arguments to be passed both to SUMO and to the partitioning script (`partRoutes.py`) after the arguments of the program, check the built-in help for more information.

To run a simple demo with a GUI:

```
./launch.sh -N 2 -c assets/simpleNet.sumocfg --gui
```

(The car will start at time 100, to give you time to reposition the windows to better look at the system)