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
#include "args.hpp"

typedef int partId_t;

/**
 * from and to are partition ids
*/
typedef struct border_edge_t {
    std::string id;
    std::vector<std::string> lanes;
    partId_t from;
    partId_t to;
} border_edge_t;

class PartitionEdgesStub {
private:
    partId_t id;
public:
    void setVehicleSpeed(std::string vehId, double speed);
    void getEdgeVehicles(std::string edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );
};

class PartitionManager : PartitionEdgesStub {
private:
    const std::string SUMO_BINARY;
    partId_t id;
    std::vector<border_edge_t> toBorderEdges;
    std::vector<border_edge_t> fromBorderEdges;
    std::string cfg;
    int port;
    int numPartitions;
    int endTime;
    std::vector<std::string> sumoArgs;
    std::string dataFolder = "";
    Args args;
    bool running;

    // handle border edges where vehicles are incoming
    void handleIncomingEdges(int, std::vector<std::vector<std::string>>&);
    // handle border edges where vehicles are outgoing
    void handleOutgoigEdges(int, std::vector<std::vector<std::string>>&);

    // connect to TraCI object
    void connect();

protected:
    // start sumo simulation in thread
    virtual void runSimulation();

public:
    // Note: public/private distinction here is mostly descriptive and not prescriptive,
    // as they are called from within the class anyways; consider public methods ones
    // that should only be called from within the instance's thread

    // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
    PartitionManager(const std::string binary, partId_t id, 
        std::string& cfg, int port, int endTime,
        std::vector<std::string> sumoArgs, Args& args);
    ~PartitionManager();
    
    /* Starts this partition in a thread. Returns true if the thread was
        successfully started, false if there was an error starting the thread */
    bool startPartition();
    // close TraCI connection, exit from thread
    void closePartition();
    // Will not return until the internal thread has exited
    void waitForPartition();
    // set this partition's border edges
    void setMyBorderEdges(std::vector<border_edge_t>&);
    void setNumPartitions(int);

    void setVehicleSpeed(std::string vehId, double speed);
    void getEdgeVehicles(std::string edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );
};