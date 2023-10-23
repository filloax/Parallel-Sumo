/**
PartitionManager.h

Class definition for PartitionManager.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/


#pragma once

#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <zmq.hpp>
#include <thread>
#include <format>

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
    const std::unordered_map<partId_t, std::unordered_set<std::string>> neighborRoutes;
    const std::unordered_map<std::string, std::unordered_set<std::string>> routeEndsInEdges;
    std::map<int, PartitionEdgesStub*> neighborPartitionStubs;
    std::map<int, NeighborPartitionHandler*> neighborClientHandlers;
    zmq::context_t& zcontext;
    // Pointer to handle ZMQ memory with certainty
    zmq::socket_t* coordinatorSocket;
    std::unordered_set<std::string> allVehicleIds;
    std::mutex allVehicleIds_lock;
    bool allVehicleIdsUpdated = false;
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
    void handleOutgoingEdges(int, std::vector<std::vector<std::string>>&);
    // barrier-like behavior via message passing
    void arriveWaitBarrier();
    // signal to main process that we finished
    void signalFinish();

    void refreshVehicleIds();

    template<typename... _Args > 
        void log(std::format_string<_Args...>  format, _Args&&... args);
    template<typename... _Args > 
        void logminor(std::format_string<_Args...>  format, _Args&&... args);
    template<typename... _Args > 
        void logerr(std::format_string<_Args...>  format, _Args&&... args);
protected:
    // start sumo simulation, already inside secondary process
    virtual void runSimulation();

public:
    // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
    PartitionManager(const std::string binary, partId_t id, std::string& cfg, int endTime,
        std::vector<partId_t>& neighborPartitions, 
        std::unordered_map<partId_t, std::unordered_set<std::string>>& neighborRoutes,
        std::unordered_map<std::string, std::unordered_set<std::string>>& routeEndsInEdges,
        zmq::context_t& zcontext, int numThreads,
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
    void setBorderEdges(std::vector<border_edge_t>&);

    #ifndef NDEBUG
        #define _str_arg_type const std::string
    #else
        #define _str_arg_type const std::string&
    #endif

    // No string ref in debug, to allow calling from lldb
    void setVehicleSpeed(_str_arg_type vehId, double speed);
    std::vector<std::string> getEdgeVehicles(_str_arg_type edgeId);
    void addVehicle(
        _str_arg_type vehId, _str_arg_type routeId, _str_arg_type vehType,
        _str_arg_type laneId, int laneIndex, double lanePos, double speed
    );
    bool hasVehicle(_str_arg_type vehId);
    bool hasVehicleInEdge(_str_arg_type vehId, _str_arg_type edgeId);

    const int getId() { return id; }
    const int getNumThreads() { return numThreads; }
    const Args& getArgs() { return args; }
};