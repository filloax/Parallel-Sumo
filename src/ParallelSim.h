/**
ParallelSim.h

Class definition for ParallelSim.

Author: Phillip Taylor
*/

#ifndef PARALLELSIM_INCLUDED
#define PARALLELSIM_INCLUDED

#include <cstdlib>
#include "libs/TraCIAPI.h"
#include "PartitionManager.h"


class ParallelSim {
  private:
    const char* SUMO_BINARY;
    const char* NETCONVERT_BINARY;
    const char* CUT_ROUTES_SCRIPT;
    TraCIAPI conn;
    std::string host;
    std::string path;
    const char* cfgFile;
    std::string netFile;
    std::string routeFile;
    std::string dataFolder;
    int port;
    int numThreads;
    std::vector<std::string>& extraArgs;
    int endTime;
    // sets the border edges for all partitions
    void setBorderEdges(std::vector<border_edge_t>[], std::vector<PartitionManager*>&);
    void loadRealNumThreads();

  public:
    ParallelSim(const std::string& host, int port, const char* file, bool gui, int threads, std::vector<std::string>& extraArgs);
    // gets network and route file paths
    void getFilePaths();
    // partition the SUMO network
    // param: true for metis partitioning, false for grid partitioning
    void partitionNetwork(bool metis, bool keepPoly);
    // execute parallel sumo simulations in created partitions
    void startSim();

};

#endif
