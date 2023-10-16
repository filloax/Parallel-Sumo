/**
PartitionManager.h

Class definition for PartitionManager.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/


#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <zmq.hpp>

#include "args.hpp"
#include "psumoTypes.hpp"
#include "partArgs.hpp"

class PartitionManager;

#include "PartitionEdgesStub.hpp"
#include "NeighborPartitionHandler.hpp"

using namespace psumo;

class PartitionManager {
private:
    const std::string binary;
    partId_t id;
    std::vector<border_edge_t> incomingBorderEdges;
    std::vector<border_edge_t> outgoingBorderEdges;
    const std::vector<partId_t> neighborPartitions;
    std::map<int, PartitionEdgesStub*> neighborPartitionStubs;
    std::map<int, NeighborPartitionHandler*> neighborClientHandlers;
    zmq::context_t& zcontext;
    zmq::socket_t coordinatorSocket;
    std::string cfg;
    int endTime;
    std::vector<std::string> sumoArgs;
    int numThreads;
    #ifdef PSUMO_SINGLE_EXECUTABLE
    Args& args;
    #else
    PartArgs& args;
    #endif
    bool running;

    // handle border edges where vehicles are incoming
    void handleIncomingEdges(int, std::vector<std::vector<std::string>>&);
    // handle border edges where vehicles are outgoing
    void handleOutgoigEdges(int, std::vector<std::vector<std::string>>&);
    // barrier-like behavior via message passing
    void arriveWaitBarrier();
    // signal to main process that we finished
    void signalFinish();

protected:
    // start sumo simulation, already inside secondary process
    virtual void runSimulation();

public:
    // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
    PartitionManager(const std::string binary, partId_t id, std::string& cfg, int endTime,
        std::vector<partId_t> neighborPartitions, zmq::context_t& zcontext, int numThreads,
        std::vector<std::string> sumoArgs, 
        #ifdef PSUMO_SINGLE_EXECUTABLE
        Args& args
        #else
        PartArgs& args
        #endif
    );
    ~PartitionManager();
    
    /* Starts this partition in a process, returning its pid. */
    int startPartitionNewProcess();
    /* Starts this partition in this process */
    void startPartitionLocalProcess();
    // set this partition's border edges
    void setMyBorderEdges(std::vector<border_edge_t>&);

    void setVehicleSpeed(const std::string& vehId, double speed);
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );

    const int getId() { return id; }
    const int getNumThreads() { return numThreads; }
    const Args getArgs() { return args; }
};