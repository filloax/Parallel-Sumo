/*
Main program for running a parallel SUMO simulation. startSim() runs simulation.
Can comment out getFilePaths() and partitionNetwork() after these functions
have already been executed to run startSim() with same partitions.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/

#include <exception>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <istream>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <argparse/argparse.hpp>
#include "partArgs.hpp"
#include "globals.hpp"
#include "src/ContextPool.hpp"
#include "utils.hpp"
#include "psumoTypes.hpp"
#include "PartitionManager.hpp"

using namespace std;
using namespace psumo;

void loadPartData(int id, string dataFolder, vector<border_edge_t>& borderEdges, vector<partId_t>& partNeighbors);

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME_PART, PROGRAM_VER);
    PartArgs args(program);

    try {
        args.parse_known_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << args.program << std::endl;
        std::exit(1);
    }

    filesystem::path dataDir(args.dataDir);
    filesystem::create_directories(dataDir / "sockets");

    stringstream partFile;
    partFile << "part" << args.partId << ".sumocfg";
    string cfg = dataDir / partFile.str();

    vector<border_edge_t> borderEdges;
    vector<partId_t> partNeighbors;

    loadPartData(args.partId, args.dataDir, borderEdges, partNeighbors);

    zmq::context_t& zctx = ContextPool::newContext(1);

    PartitionManager partManager(
        getSumoPath(args.gui), args.partId, cfg, args.endTime,
        partNeighbors, zctx, args.numThreads,
        args.sumoArgs, args 
    );
    partManager.setMyBorderEdges(borderEdges);

    try {
        partManager.startPartitionLocalProcess();
    } catch (exception& e) {
        stringstream msg;
        msg << endl << "[ERR] Partition " << args.partId << " terminating because of an error: "
            << e.what() << endl;
    }

    // Deleting context blocks forever
    // ContextPool::destroyAll();
}

void loadPartData(int id, string dataFolder, vector<border_edge_t>& borderEdges, vector<partId_t>& partNeighbors) {
    const auto dataFile = getPartitionDataFile(dataFolder, id);
    
    ifstream input(dataFile);
    if (!input) {
        std::cerr << "Failed to open the data file: " << dataFile << std::endl;
        exit(-2);
    }

    nlohmann::json data;
    try {
        input >> data;
    } catch(const exception& e) {
        std::cerr << "Failed to parse data file JSON: " << e.what() << std::endl;
        exit(-3);
    }
    input.close();

    borderEdges = data["borderEdges"].template get<vector<border_edge_t>>();
    partNeighbors = data["neighbors"].template get<vector<partId_t>>();
}