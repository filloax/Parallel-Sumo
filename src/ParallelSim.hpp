/**
ParallelSim.h

Class definition for ParallelSim.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/

#pragma once

#include <cstdlib>
#include "libs/traciapi/TraCIAPI.h"
#include "PartitionManager.hpp"
#include "args.hpp"

typedef struct border_edge_indices_t {
    std::string id;
    std::vector<std::string> lanes;
    int from;
    int to;
} border_edge_indices_t;

class ParallelSim {
  private:
    std::string SUMO_BINARY;
    TraCIAPI conn;
    std::string host;
    std::string path;
    std::string cfgFile;
    std::string netFile;
    std::string routeFile;
    std::string dataFolder;
    int port;
    int numThreads;
    std::vector<std::string>& sumoArgs;
    int endTime;
    Args args;
    // sets the border edges for all partitions
    void calcBorderEdges(std::vector<std::vector<border_edge_indices_t>>& borderEdgesIndices, std::vector<std::vector<int>>& partNeighbors);
    void loadRealNumThreads();

  public:
    ParallelSim(const std::string& host, int port, const std::string file, bool gui, int threads, std::vector<std::string>& sumoArgs, Args& args);
    // gets network and route file paths
    void getFilePaths();
    // partition the SUMO network
    // param: true for metis partitioning, false for grid partitioning
    void partitionNetwork(bool metis, bool keepPoly);
    // execute parallel sumo simulations in created partitions
    void startSim();

};
