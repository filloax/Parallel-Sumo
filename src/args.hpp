#pragma once

#include <libs/argparse.hpp>

class Args {
public:
    Args(argparse::ArgumentParser& program):
    program(program)
    {
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
        program.add_argument("--part-threads")
            .help("Threads used while partitioning")
            .default_value(2)
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
    }

    void parse_known_args(int argc, char* argv[]) {
        sumoArgs = program.parse_known_args(argc, argv);
        
        port = program.get<int>("--port");
        cfg = program.get<std::string>("--cfg");
        numThreads = program.get<int>("--num-threads");
        partitioningThreads = program.get<int>("--part-threads");
        gui = program.get<bool>("--gui");
        skipPart = program.get<bool>("--skip-part");
        keepPoly = program.get<bool>("--keep-poly");

        if (numThreads <= 0) {
            std::cerr << "Error: wrong number of threads, must be positive number (can be 1 for testing), is " << numThreads << std::endl;
            exit(-1);
        }
        if (partitioningThreads <= 0) {
            std::cerr << "Error: wrong number of partitioning threads, must be positive number (can be 1 for testing), is " << partitioningThreads << std::endl;
            exit(-1);
        }

        std::cout << "port=" << port << ", cfg=" << cfg
            << ", numThreads=" << numThreads << ", partitioningThreads=" << partitioningThreads
            << ", gui=" << gui << ", skipPart=" << skipPart
            << ", keepPoly=" << keepPoly
            << std::endl;
    }

    int port;
    std::string cfg;
    int numThreads;
    int partitioningThreads;
    bool gui;
    bool skipPart;
    bool keepPoly;
    std::vector<std::string> sumoArgs;
    argparse::ArgumentParser& program;
};