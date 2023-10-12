#pragma once

#include <string>
#include <vector>

#include "libs/traciapi/TraCIAPI.h"

#define ROUTER_OWNER -1

typedef struct partitionPort {
    int partIdx;
    int port;
} partitionPort;

/**
 * Used by partition managers to get a connection for each partition's SUMO instance.
 * Connects to the partition's simulation plus its neighbors.
*/
class SumoConnectionRouter {

/* 
Possible improvements later: add checking to see if
the partition has "permissions" to do non-whitelisted
operations, only allowing some to be done in the not-owned
connection. Could be a performance issue, so avoid for now.

Every connection operation defaults to -1 as the partition index, 
*/

private:
    // Can be -1 for no owner, used for defaulting operations
    int ownerId;

    std::vector<int> handledPartitions;
    // idx = partition index
    std::vector<TraCIAPI*> connections;
    // -1 if unhandled partition
    std::vector<int> partitionPorts;
    std::string host;

    void connectToPartition(int partId);
    void closePartition(int partId);
    bool handlesPartition(int partId);
public:
    /**
     * @param ownerId Used to default the operation to the owner and mainly simulation step operations/execution order
     * @param host hostname
     * @param partitionPorts vector with the pair of partition/port of each neighbor or owner partition
    */
    SumoConnectionRouter(int ownerId, std::string host, std::vector<partitionPort>& partitionPorts, int numParts);
    ~SumoConnectionRouter();

    void connectAll();
    void closeAll();

    // -1 = use owner id
    
    // get vehicles on edge
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId, int partId = ROUTER_OWNER);
    // get edges of route
    std::vector<std::string> getRouteEdges(const std::string& routeId, int partId = ROUTER_OWNER);
    // move vehicle to specified position on lane
    void moveTo(const std::string& vehID, const std::string& laneID, double pos, int partId = ROUTER_OWNER);
    // set vehicle speed to propagate traffic conditions in next partition
    void slowDown(const std::string&, double, int partId = ROUTER_OWNER);
    // add vehicle into simulation
    void addVehicle(const std::string& vehID, const std::string& routeID, const std::string& typeID,
        const std::string& laneInd, const std::string& depPos, 
        const std::string& speed, int partId = ROUTER_OWNER);
    std::string getVehicleRouteId(std::string vehicleId, int partId = ROUTER_OWNER);
    float getVehicleSpeed(std::string vehicleId, int partId = ROUTER_OWNER);
    std::string getVehicleType(std::string vehicleId, int partId = ROUTER_OWNER);
    int getVehicleLaneIndex(std::string vehicleId, int partId = ROUTER_OWNER);
    std::string getVehicleLaneId(std::string vehicleId, int partId = ROUTER_OWNER);
    double getVehicleLanePosition(std::string vehicleId, int partId = ROUTER_OWNER);
    // Only on router owner
    float getSimulationTime();
    void simulationStep();
};