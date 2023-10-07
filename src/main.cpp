/*
Main program for running a parallel SUMO simulation. startSim() runs simulation.
Can comment out getFilePaths() and partitionNetwork() after these functions
have already been executed to run startSim() with same partitions.

Author: Phillip Taylor
*/

#include <iostream>
#include <cstdlib>
#include <vector>
#include "ParallelSim.h"
#include <libs/argparse.hpp>
#include "args.hpp"

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("parallel-sumo", "0.4");
    Args args(program);

    try {
        args.parse_known_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << args.program << std::endl;
        std::exit(1);
    }

    // params: host server, first port. sumo cfg file, gui option (true), number of threads
    ParallelSim client(args.host, args.port, args.cfg.c_str(), args.gui, args.numThreads, args.sumoArgs, args);
    client.getFilePaths();
    if (!args.skipPart && args.numThreads > 1) {
        // param: true for metis partitioning, false for grid partitioning (only works for 2 partitions currently)
        // edit: grid implemented by original designer, currently not tested
        client.partitionNetwork(true, args.keepPoly);
    }
    client.startSim();
}
