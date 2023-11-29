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
    std::vector<std::string> argv_;
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
        program.add_argument("--remote-port")
            .help("First remote port that will be used to host TraCI servers in the partitions. Each partition will host it at <this value>+<partition number>, where partition number starts from 0. If not set, no TraCI server will be started.\nNote that doing this will made each partition wait for a client to take control and manually allow them to proceed.")
            .default_value(-1)
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
        program.add_argument("--pin-to-cpu")
            .help("Force each partition to run on one CPU only (will error in N > n° cpus)")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--log-handled-vehicles")
            .help("Print a text file in data with the number of handles vehicles at each simulation step for a partition")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--log-msg-num")
            .help("Print a text file with the number of messages passed in each iteration")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--data-dir")
            .help("Data directory to store working files in")
            .default_value("data");
        program.add_argument("-v", "--verbose")
            .help("Extra output")
            .default_value(false)
            .implicit_value(true);
    }

    void parse_known_args(int argc, char* argv[]) {
        for (int i = 1; i < argc; i++) {
            argv_.push_back(argv[i]);
        }

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
        remotePort = program.get<int>("--remote-port");
        gui = program.get<bool>("--gui");
        skipPart = program.get<bool>("--skip-part");
        keepPoly = program.get<bool>("--keep-poly");
        pinToCpu = program.get<bool>("--log-handled-vehicles");
        logHandledVehicles = program.get<bool>("--pin-to-cpu");
        logMsgNum = program.get<bool>("--log-msg-num");
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

    std::vector<std::string>& getArgVector() {
        return argv_;
    }

    std::string cfg;
    int numThreads;
    int partitioningThreads;
    int remotePort;
    bool gui;
    bool skipPart;
    bool keepPoly;
    bool pinToCpu;
    bool logHandledVehicles;
    bool logMsgNum;
    std::string dataDir;
    bool verbose;
    std::vector<std::string> sumoArgs;
    std::vector<std::string> partitioningArgs;
    argparse::ArgumentParser& program;
};