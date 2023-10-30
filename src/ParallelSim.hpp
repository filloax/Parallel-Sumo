/**
ParallelSim.h

Class definition for ParallelSim.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/

#pragma once

#include <cstdlib>
#include <zmq.hpp>
#include "args.hpp"
#include "psumoTypes.hpp"

class ParallelSim {
  private:
    #ifdef PSUMO_SINGLE_EXECUTABLE
    std::string SUMO_BINARY;
    #endif
    std::string path;
    std::string cfgFile;
    std::string netFile;
    std::string routeFile;
    int numThreads;
    int endTime;
    int steps;
    int syncBarrierTimes;
    Args args;
    // sets the border edges for all partitions
    void calcBorderEdges(std::vector<std::vector<psumo::border_edge_t>>& borderEdges, std::vector<std::vector<psumo::partId_t>>& partNeighbors);
    void loadRealNumThreads();

    void coordinatePartitionsSync(zmq::context_t&);
    void waitForPartitions(std::vector<pid_t> pids);

  public:
    ParallelSim(const std::string file, bool gui, int threads, Args& args);
    // gets network and route file paths
    void getFilePaths();
    // partition the SUMO network
    // param: true for metis partitioning, false for grid partitioning
    void partitionNetwork(bool metis, bool keepPoly);
    // execute parallel sumo simulations in created partitions
    void startSim();


    enum SyncOps {
        BARRIER,
        BARRIER_STEP,
        FINISHED
    };
};
