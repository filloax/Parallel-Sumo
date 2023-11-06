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
    std::string SUMO_BINARY;
    std::string path;
    std::string cfgFile;
    std::string netFile;
    std::string routeFile;
    int numThreads;
    int endTime;
    int steps;
    int syncBarrierTimes;
    bool allFinished = false;
    Args args;
    // sets the border edges for all partitions
    void calcBorderEdges(std::vector<std::vector<psumo::border_edge_t>>& borderEdges, std::vector<std::vector<psumo::partId_t>>& partNeighbors);
    void loadRealNumThreads();

    int coordinatePartitionsSync(zmq::context_t&, std::shared_ptr<zmq::socket_t> controlSocket);
    void waitForPartitions(std::vector<pid_t> pids, std::shared_ptr<zmq::socket_t> controlSocket);

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
