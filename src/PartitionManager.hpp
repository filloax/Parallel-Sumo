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
#include <thread>
#include <barrier>
#include "libs/traciapi/TraCIAPI.h"
#include "args.hpp"
#include "SumoConnectionRouter.hpp"

typedef struct border_edge_t border_edge_t;

class PartitionManager {
private:
    const std::string SUMO_BINARY;
    int id;
    SumoConnectionRouter& router;
    std::vector<border_edge_t> toBorderEdges;
    std::vector<border_edge_t> fromBorderEdges;
    std::string cfg;
    int port;
    int endTime;
    std::vector<std::string> sumoArgs;
    std::string dataFolder = "";
    std::thread thread;
    TraCIAPI myConn;
    Args args;
    bool running;
    std::barrier<>& syncBarrier; //C++20

    // handle border edges where vehicles are incoming
    void handleIncomingEdges(int, std::vector<std::vector<std::string>>&);
    // handle border edges where vehicles are outgoing
    void handleOutgoigEdges(int, std::vector<std::vector<std::string>>&);

    // connect to TraCI object
    void connect();
    // get edges of route
    std::vector<std::string> getRouteEdges(const std::string&);
    // move vehicle to specified position on lane
    void moveTo(const std::string&, const std::string&, double);
    // set vehicle speed to propagate traffic conditions in next partition
    void slowDown(const std::string&, double);
    // add vehicle into simulation
    void add(const std::string&, const std::string&, const std::string&,
      const std::string&, const std::string&, const std::string&);


protected:
    // start sumo simulation in thread
    virtual void internalSim();

public:
    // Note: public/private distinction here is mostly descriptive and not prescriptive,
    // as they are called from within the class anyways; consider public methods ones
    // that should only be called from within the instance's thread

    // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
    PartitionManager(const std::string binary, int id, std::barrier<>& syncBarrier,
        SumoConnectionRouter& router, std::string& cfg, int port, int t,
        std::vector<std::string> sumoArgs, Args& args);
    
    /* Starts this partition in a thread. Returns true if the thread was
        successfully started, false if there was an error starting the thread */
    bool startPartition();
    // close TraCI connection, exit from thread
    void closePartition();
    // Will not return until the internal thread has exited
    void waitForPartition();
    // set this partition's border edges
    void setMyBorderEdges(std::vector<border_edge_t>&);

    // get vehicles on edge
    std::vector<std::string> getEdgeVehicles(const std::string&);
};

struct border_edge_t {
    std::string id;
    std::vector<std::string> lanes;
    PartitionManager* from;
    PartitionManager* to;
};
