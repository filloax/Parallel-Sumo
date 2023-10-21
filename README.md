# Parallel-Sumo Tweaks

Tweaks to the Parallel-Sumo repo as part of a thesis work, to study possibilities of parallelization in SUMO.

Remember to set the SUMO_HOME environment variable to the directory containing SUMO!

### Installation and dependencies

First, you need to install Eclipse [SUMO](https://eclipse.dev/sumo/), and the [Boost](https://www.boost.org/) libraries. This version of Parallel-Sumo also requires the [pymetis](https://github.com/inducer/pymetis) python package, which requires python from 3.4 to 3.10. Check the repository page for install instructions especially on Windows, will need to download either it with conda from conda-forge (the other binary option in its repo is outdated). We include a *install-pymetis.ps1* Powershell script to do that.

### How to compile:

First, you need to install ZeroMQ on your system, to have it available as a dependency, and also CMake. On Arch (both as a OS install or using [ArchWSL](https://github.com/yuk7/ArchWSL) in Windows with WSL), for example, install the following packages:

```
sudo pacman -S zeromq cppzeromq nlohmann-json clang ninja cmake 
```

SUMO can be installed via various means depending on your OS distribution, check its website linked above. I personally used `yay -S sumo` in Arch/WSL. If your SUMO installation doesn't include libsumo headers, you also will need to install its source in the $SUMO_HOME/src folder.

The general process for building is:
- `cd` to the `build` folder
- Run `cmake .. -GNinja`
- Run `ninja`

It is recommended to use Linux, or WSL if you're on Windows. Currently uses POSIX functions (mainly fork) that are hard to get rid of without rewriting more of the program, so Visual Studio compilation is not yet supported.

<details markdown="1">
<summary>Old methods</summary>

**On Windows**: approach I used was
- Install C compilers and toolkit via [MSYS2](https://www.msys2.org/), follow for instance the [VSCode guide to C++ dev on Windows](https://code.visualstudio.com/docs/cpp/config-mingw)
- Install **in the MSYS2 environment**\* (instead of UCRT64, as MSYS2 is POSIX-like) the base-devel and gcc packages: `pacman -S base-devel gcc`
- Optional: add msys64/usr/bin folder to PATH
- Run `make -f Makefile_win` command either in Powershell with msys folders in path, or from the msys2 terminal. Make sure you're running it in the project's folder!
</details>

---

# Original Readme

# Parallel-Sumo
A multithreaded C++ and python implementation to parallelize SUMO (Simulation of Urban Mobility).

# Requirements
SUMO (with home environment variable set), C++ compiler, python3, METIS.

SUMO routes must be explicit for every vehicle, and does not yet support additionals (taz, detectors).

# How to use
Edit the main.cpp file with the host server, port, path to the SUMO config file (with all other SUMO files in same path), and desired number of threads. Compile with the command 'make main', and run the main executable.
