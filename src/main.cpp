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

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("parallel-sumo", "0.3");

    program.add_argument("-h", "--host")
        .help("IP for the host server")
        .default_value("localhost");
    program.add_argument("-p", "--port")
        .help("Port for the first thread's server")
        .default_value(1337)
        .scan<'i', int>();
        ;
    program.add_argument("-c", "--cfg")
        .help("Sumo config path")
        // For demo purposes
        .default_value("assets/simpleNet.sumocfg");
    program.add_argument("-N", "--num-threads")
        .help("Thread num")
        .default_value(4)
        .scan<'i', int>();
        ;
    program.add_argument("--skip-part")
        .help("Skip partitioning (needs to already have run the program without this option before)")
        .default_value(false)
        .implicit_value(true);

    std::vector<std::string> sumoArgs;
    try {
        sumoArgs = program.parse_known_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    auto skipPart = program.get<bool>("--skip-part");
    auto host = program.get<std::string>("--host");
    auto port = program.get<int>("--port");
    auto cfg = program.get<std::string>("--cfg");
    auto numThreads = program.get<int>("--num-threads");

    // params: host server, first port. sumo cfg file, gui option (true), number of threads
    ParallelSim client(host, port, cfg.c_str(), true, numThreads, sumoArgs);
    client.getFilePaths();
    // param: true for metis partitioning, false for grid partitioning (only works for 2 partitions currently)
    if (!skipPart) {
        client.partitionNetwork(true);
    }
    client.startSim();
}
