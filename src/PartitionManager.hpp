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

typedef int partId_t;
class PartitionManager;

#include "PartitionEdgesStub.hpp"
#include "NeighborPartitionHandler.hpp"

typedef struct border_edge_t {
    std::string id;
    std::vector<std::string> lanes;
    partId_t from;
    partId_t to;
} border_edge_t;

class PartitionManager {
private:
    const std::string binary;
    partId_t id;
    std::vector<border_edge_t> toBorderEdges;
    std::vector<border_edge_t> fromBorderEdges;
    const std::vector<partId_t> neighborPartitions;
    std::map<int, PartitionEdgesStub*> neighborPartitionStubs;
    std::map<int, NeighborPartitionHandler*> neighborClientHandlers;
    zmq::context_t zcontext;
    std::string cfg;
    int endTime;
    std::vector<std::string> sumoArgs;
    std::string dataFolder = "";
    Args args;
    bool running;

    // handle border edges where vehicles are incoming
    void handleIncomingEdges(int, std::vector<std::vector<std::string>>&);
    // handle border edges where vehicles are outgoing
    void handleOutgoigEdges(int, std::vector<std::vector<std::string>>&);

protected:
    // start sumo simulation in thread
    virtual void runSimulation();

public:
    // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
    PartitionManager(const std::string binary, partId_t id, std::string& cfg, int endTime,
        std::vector<partId_t> neighborPartitions,
        std::vector<std::string> sumoArgs, Args& args);
    ~PartitionManager();
    
    /* Starts this partition in a process, returning its pid. */
    int startPartition();
    // close TraCI connection, exit from thread
    void closePartition();
    // Will not return until the internal thread has exited
    void waitForPartition();
    // set this partition's border edges
    void setMyBorderEdges(std::vector<border_edge_t>&);

    void setVehicleSpeed(const std::string& vehId, double speed);
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );

    const int getId() { return id; }
    const Args getArgs() { return args; }
};