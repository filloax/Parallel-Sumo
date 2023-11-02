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
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "libs/argparse.hpp"

#include "partArgs.hpp"
#include "globals.hpp"
#include "ContextPool.hpp"
#include "utils.hpp"
#include "psumoTypes.hpp"
#include "PartitionManager.hpp"
#include "utils.hpp"

using namespace std;
using namespace psumo;

void loadPartData(int id, string dataFolder, vector<border_edge_t>& borderEdges, vector<partId_t>&, unordered_map<partId_t, unordered_set<string>>&, unordered_map<string, unordered_set<string>>&, float*);

int main(int argc, char* argv[]) {
    #ifdef HAVE_LIBSUMOGUI
        argparse::ArgumentParser program(PROGRAM_NAME_PART_GUI, PROGRAM_VER);
    #else
        argparse::ArgumentParser program(PROGRAM_NAME_PART, PROGRAM_VER);
    #endif
    PartArgs args(program);

    try {
        args.parse_known_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << args.program << std::endl;
        std::exit(1);
    }

    if (args.pinToCpu) {
        psumo::bindProcessToCPU(args.partId);
        printf("Pinned partition %d to cpu %d\n", args.partId, args.partId);
    }

    ContextPool::verbose = args.verbose;

    filesystem::path dataDir(args.dataDir);
    filesystem::create_directories(dataDir / "sockets");

    stringstream partFile;
    partFile << "part" << args.partId << ".sumocfg";
    string cfg = dataDir / partFile.str();

    vector<border_edge_t> borderEdges;
    vector<partId_t> partNeighbors;
    unordered_map<partId_t, unordered_set<string>> partNeighborRoutes;
    unordered_map<string, unordered_set<string>> routesEndingInEdge;
    float lastDepartTime;

    if (args.numThreads > 1) {
        loadPartData(args.partId, 
            args.dataDir, borderEdges, partNeighbors, 
            partNeighborRoutes, routesEndingInEdge, 
            &lastDepartTime
        );
    } else {
        cout << "Starting partition in 1 thread mode (almost no special treatment, more or less base sumo run)" << endl;
        cfg = args.cfg; 
    }

    zmq::context_t& zctx = ContextPool::newContext(1);

    PartitionManager partManager(
        getSumoPath(args.gui), args.partId, cfg, args.endTime,
        partNeighbors, partNeighborRoutes, 
        routesEndingInEdge, lastDepartTime,
        zctx, args.numThreads,
        args.sumoArgs, args 
    );
    partManager.setBorderEdges(borderEdges);
    partManager.loadRouteMetadata();
    partManager.enableTimeMeasures();

    try {
        partManager.startPartitionLocalProcess();
    } catch (exception& e) {
        stringstream msg;
        msg << endl << "[ERR] Partition " << args.partId << " terminating because of an error: "
            << e.what() << endl;
    }

    // Deleting context blocks forever
    if (args.verbose) {
        printf("\tPartition %d process %d clearing zmq contexts\n", args.partId, getPid());
    }
    ContextPool::destroyAll();
    if (args.verbose) {
        printf("\tPartition %d process %d ended\n", args.partId, getPid());
    }
    return 0;
}

void loadPartData(
    int id, string dataFolder, 
    vector<border_edge_t>& borderEdges, 
    vector<partId_t>& partNeighbors,
    unordered_map<partId_t, unordered_set<string>>& partNeighborRoutes,
    unordered_map<string, unordered_set<string>>& routesEndingInEdge,
    float* lastDepartTime
) {
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

    map<string, vector<string>> partNeighborLists = data["neighborRoutes"].template get<map<string, vector<string>>>();
    for (auto pair : partNeighborLists) {
        auto neighIdString = pair.first;
        auto routesVector = pair.second;
        
        partId_t neighId = stoi(neighIdString);
        unordered_set<string> neighRoutes(routesVector.begin(), routesVector.end());
        partNeighborRoutes[neighId] = neighRoutes;
    }

    routesEndingInEdge = data["borderRouteEnds"].template get<unordered_map<string, unordered_set<string>>>();
    *lastDepartTime = data["lastDepart"].template get<float>();
}