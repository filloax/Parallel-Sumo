/*
Main program for running a parallel SUMO simulation. startSim() runs simulation.
Can comment out getFilePaths() and partitionNetwork() after these functions
have already been executed to run startSim() with same partitions.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/

#include <exception>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <filesystem>
#include "libs/argparse.hpp"
#include "ParallelSim.hpp"
#include "args.hpp"
#include "globals.hpp"

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME, PROGRAM_VER);
    Args args(program);

    try {
        args.parse_known_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << args.program << std::endl;
        std::exit(1);
    }

    std::filesystem::path dataDir(args.dataDir);
    try {
        std::filesystem::remove_all(dataDir / "sockets");
    } catch (std::exception& e) {}
    std::filesystem::create_directories(dataDir / "sockets");
    try {
        for(auto const& entry: std::filesystem::directory_iterator{OUTDIR}) {
            if (entry.path().extension() == ".xml")
                std::filesystem::remove(entry.path());
        }
    } catch (std::exception& e) {}
    std::filesystem::create_directories(OUTDIR);

    // params: host server, first port. sumo cfg file, gui option (true), number of threads
    ParallelSim client(args.cfg.c_str(), args.gui, args.numThreads, args);
    client.getFilePaths();
    if (!args.skipPart) { //&& args.numThreads > 1) {
        // param: true for metis partitioning, false for grid partitioning (only works for 2 partitions currently)
        // edit: grid implemented by original designer, currently not tested
        // one thread partitioning just processes the demand files as they would be for partitions
        client.partitionNetwork(true, args.keepPoly);
    }
    client.startSim();
}
