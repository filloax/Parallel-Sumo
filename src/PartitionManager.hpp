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

/**
 * from and to are partition ids
*/
typedef struct border_edge_t {
    std::string id;
    std::vector<std::string> lanes;
    int from;
    int to;
} border_edge_t;

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
};