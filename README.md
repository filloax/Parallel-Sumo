# Parallel-Sumo Tweaks

Tweaks to the Parallel-Sumo repo as part of a thesis work, to study possibilities of parallelization in SUMO.

Remember to set the SUMO_HOME environment variable to the directory containing SUMO!

### Installation and dependencies

First, you need to install Eclipse [SUMO](https://eclipse.dev/sumo/). This version of Parallel-Sumo also requires the [metis](https://github.com/inducer/pymetis) python package. This as the original problem needs METIS to be installed and be available on the PATH, the source can be downloaded [here](http://glaros.dtc.umn.edu/gkhome/metis/metis/download), both for Windows and for Linux.
- Note: installing METIS on Windows is difficult. Personally I managed to do it by downloading old Visual Studio and CMake versions (2010 and 2.8 respectively) to match the build instructions in the METIS download from its main website, otherwise [this repository](https://github.com/guglielmosanchini/conda-metis) could be of help. You need to end up with a metis.dll file (and set the `METIS_DLL` environment variable to its full path), depending on your compile method you might need to enable SHARED in the compile settings to end up with a dll.

### How to compile:

**On Linux**: running `make` on the root folder should be enough.

**On Windows**: approach I used was
- Install C compilers and toolkit via [MSYS2](https://www.msys2.org/), follow for instance the [VSCode guide to C++ dev on Windows](https://code.visualstudio.com/docs/cpp/config-mingw)
- Install **in the MSYS2 environment**\* (instead of UCRT64) the base-devel and gcc packages: `pacman -S base-devel gcc`
- Optional: add msys64/usr/bin folder to PATH
- Run `make -f Makefile_win` command either in Powershell msys folders to path, or from the msys2 terminal. Make sure you're running it in the project's folder!

\* In general, you need a C++ environment with the standard POSIX libraries like *sys/wait.h* available, while Windows by default doesn't have it, and neither does UCRT64.  The MSYS2 default environment has the correct libraries set up.  
Another way to compile this is using Cygwin, but it does have the side effect of requiring the program to be run through it and having X-style windows.


---

# Original Readme

# Parallel-Sumo
A multithreaded C++ and python implementation to parallelize SUMO (Simulation of Urban Mobility).

# Requirements
SUMO (with home environment variable set), C++ compiler, python3, METIS.

SUMO routes must be explicit for every vehicle, and does not yet support additionals (taz, detectors).

# How to use
Edit the main.cpp file with the host server, port, path to the SUMO config file (with all other SUMO files in same path), and desired number of threads. Compile with the command 'make main', and run the main executable.
