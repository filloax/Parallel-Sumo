/**
args.hpp

Wrapper for args, to access them in a single object.

Author: Filippo Lenzi
*/

#pragma once

#include "libs/argparse.hpp"
#include <cstdlib>
#include <sstream>

class Args {
protected:
    bool printOnParse = true;
public:
    Args(argparse::ArgumentParser& program):
    program(program)
    {
        program.add_description("Run the traffic simulation program SUMO in parallel using multiple processes");
        program.add_epilog("Additional arguments can be added, optionally separated by a pipe ('--').\nArguments before the pipe (or all of them without a pipe) are passed to the SUMO executable, and arguments after are passed to the createParts.py Python script (run './run-with-env.sh python scripts/createParts.py' --help to check available options).");
        program.add_argument("-c", "--cfg")
            .help("Sumo config path")
            // For demo purposes
            .default_value("assets/simpleNet.sumocfg");
        program.add_argument("-N", "--num-threads")
            .help("Thread num")
            .default_value(4)
            .scan<'i', int>();
            ;
        program.add_argument("--part-threads")
            .help("Threads used while partitioning (will be capped to partition amount)")
            .default_value(8)
            .scan<'i', int>();
            ;
        program.add_argument("--gui")
            .help("Displays SUMO gui. Note that this launches one GUI application per thread.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--skip-part")
            .help("Skip partitioning (needs to already have run the program without this option before)")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--keep-poly")
            .help("Keep poly data if present in the original sumocfg (False by default for performance)")
            .default_value(false)
            .implicit_value(true);
        #ifndef PSUMO_SINGLE_EXECUTABLE
        program.add_argument("--pin-to-cpu")
            .help("Force each partition to run on one CPU only (will error in N > n° cpus)")
            .default_value(false)
            .implicit_value(true);
        #endif
        program.add_argument("--data-dir")
            .help("Data directory to store working files in")
            .default_value("data");
        program.add_argument("-v", "--verbose")
            .help("Extra output")
            .default_value(false)
            .implicit_value(true);
    }

    void parse_known_args(int argc, char* argv[]) {
        auto extraArgs = program.parse_known_args(argc, argv);
        // Split partitioning args and sumo args via extra args
        auto pipeIt = std::find(extraArgs.begin(), extraArgs.end(), "--");
        sumoArgs.insert(sumoArgs.begin(), extraArgs.begin(), pipeIt);
        if (pipeIt != extraArgs.end()) {
            partitioningArgs.insert(partitioningArgs.begin(), pipeIt + 1, extraArgs.end());
        }
        
        cfg = program.get<std::string>("--cfg");
        numThreads = program.get<int>("--num-threads");
        partitioningThreads = program.get<int>("--part-threads");
        gui = program.get<bool>("--gui");
        skipPart = program.get<bool>("--skip-part");
        keepPoly = program.get<bool>("--keep-poly");
        #ifndef PSUMO_SINGLE_EXECUTABLE
        pinToCpu = program.get<bool>("--pin-to-cpu");
        #endif
        dataDir = program.get<std::string>("--data-dir");
        verbose = program.get<bool>("--verbose");

        std::stringstream msg;
        if (numThreads <= 0) {
            msg << "Error: wrong number of threads, must be positive number (can be 1 for testing), is " << numThreads << std::endl;
            std::cerr << msg.str();
            exit(EXIT_FAILURE);
        }
        if (partitioningThreads <= 0) {
            msg << "Error: wrong number of partitioning threads, must be positive number (can be 1 for testing), is " << partitioningThreads << std::endl;
            std::cerr << msg.str();
            exit(EXIT_FAILURE);
        }

        if (printOnParse) {
            std::cout << "cfg=" << cfg << ", numThreads=" << numThreads 
                << ", partitioningThreads=" << partitioningThreads
                << ", gui=" << gui << ", skipPart=" << skipPart
                << ", keepPoly=" << keepPoly << ", dataDir=" << dataDir
                << ", verbose=" << verbose
                << std::endl;
        }
    }

    std::string cfg;
    int numThreads;
    int partitioningThreads;
    bool gui;
    bool skipPart;
    bool keepPoly;
    #ifndef PSUMO_SINGLE_EXECUTABLE
    bool pinToCpu;
    #endif
    std::string dataDir;
    bool verbose;
    std::vector<std::string> sumoArgs;
    std::vector<std::string> partitioningArgs;
    argparse::ArgumentParser& program;
};