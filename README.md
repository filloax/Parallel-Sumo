# Parallel-Sumo Tweaks

Tweaks to the Parallel-Sumo repo as part of a thesis work, to study possibilities of parallelization in SUMO.

Remember to set the SUMO_HOME environment variable to the directory containing SUMO!

### Installation and dependencies

First, you need to install Eclipse [SUMO](https://eclipse.dev/sumo/), and the [Boost](https://www.boost.org/) libraries. Currently, the launch script will automatically download the required Python libraries.

## How-to-use

Launch the script `launch.sh` with **--help** as an argument to check all available options. The main args are **-c** to specify the .sumocfg file to run the simulation from, **-N** to specify the number of processes, and **--gui** if you want to see a GUI of the partition. You can specify arguments to be passed both to SUMO and to the partitioning script (`partRoutes.py`) after the arguments of the program, check the built-in help for more information.

To run a simple demo with a GUI:

```
./launch.sh -N 2 -c assets/simpleNet.sumocfg --gui
```

(The car will start at time 100, to give you time to reposition the windows to better look at the system)

### Binaries

Binaries are automatically built with GitHub actions, latest version can be found [here](https://github.com/filloax/Parallel-Sumo/suites/17581327676/artifacts/1004545522), but check in the [Actions tab](https://github.com/filloax/Parallel-Sumo/actions/workflows/build.yml) if you want to be sure it is the latest one, as the link is updated manually.

<details markdown="1">
<summary>How-to</summary>

The binaries are built on Ubuntu by GitHub and should work in any Linux x86_64 system. Download/clone this repository, create a bin folder inside it, download the binaries in the bin folder and you should be able to run launch.sh.

</details>

### How to compile:

First, you need to install ZeroMQ on your system, to have it available as a dependency, and also CMake. On Arch (both as a OS install or using [ArchWSL](https://github.com/yuk7/ArchWSL) in Windows with WSL), for example, install the following packages:

```
sudo pacman -S zeromq cppzmq nlohmann-json boost clang ninja cmake 
```

On Ubuntu/distros with apt:

```
sudo add-apt-repository ppa:sumo/stable && sudo apt update
sudo apt install sumo sumo-tools sumo-doc libzmq3-dev ninja-build nlohmann-json3-dev libboost-all-dev cmake libc++-dev
```

SUMO can be installed via various means depending on your OS distribution, check its website linked above. I personally used `yay -S sumo` in Arch/WSL. If your SUMO installation doesn't include libsumo headers, you also will need to install its source in the $SUMO_HOME/src folder.

The general process for building is:
- Run `configure.sh` in the project root
- `cd` to the `build` folder
- Run `ninja`

It is recommended to use Linux, or WSL if you're on Windows. Currently uses POSIX functions (mainly fork) that are hard to get rid of without rewriting more of the program, so Visual Studio compilation is not yet supported.

In case you installed some of these by non-standard sources, some mingling with CMake settings might be required to add them to the compilation path.

<details markdown="1">
<summary>Old methods</summary>

**On Windows**: approach I used was
- Install C compilers and toolkit via [MSYS2](https://www.msys2.org/), follow for instance the [VSCode guide to C++ dev on Windows](https://code.visualstudio.com/docs/cpp/config-mingw)
- Install **in the MSYS2 environment**\* (instead of UCRT64, as MSYS2 is POSIX-like) the base-devel and gcc packages: `pacman -S base-devel gcc`
- Optional: add msys64/usr/bin folder to PATH
- Run `make -f Makefile_win` command either in Powershell with msys folders in path, or from the msys2 terminal. Make sure you're running it in the project's folder!
</details>


### Compiling errors

If you get errors such as `'format' file not found`, you probably have a old compiler. G++ 12 or Clang++ 14 are required.

---

# Original Readme

# Parallel-Sumo
A multithreaded C++ and python implementation to parallelize SUMO (Simulation of Urban Mobility).

# Requirements
SUMO (with home environment variable set), C++ compiler, python3, METIS.

SUMO routes must be explicit for every vehicle, and does not yet support additionals (taz, detectors).

# How to use
Edit the main.cpp file with the host server, port, path to the SUMO config file (with all other SUMO files in same path), and desired number of threads. Compile with the command 'make main', and run the main executable.
