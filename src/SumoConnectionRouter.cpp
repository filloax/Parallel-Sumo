#include "SumoConnectionRouter.hpp"
#include "utils.hpp"
#include "libs/traciapi/TraCIAPI.h"
#include <exception>

SumoConnectionRouter::SumoConnectionRouter(std::string host, std::vector<partitionPort>& _partitionPorts, int numParts, int ownerId):
host(host),
ownerId(ownerId)
{
    connections.reserve(numParts);
    partitionPorts.reserve(numParts);
    for (auto partitionPort : _partitionPorts) {
        handledPartitions.push_back(partitionPort.partIdx);
        partitionPorts[partitionPort.partIdx] = partitionPort.port;
        connections[partitionPort.partIdx] = new TraCIAPI();
    }
    for (int i = 0; i < numParts; i++) {
        // partition not in handled ones
        if (std::find(handledPartitions.begin(), handledPartitions.end(), i) == handledPartitions.end()) {
            partitionPorts[i] = -1;
        }
    }
}

SumoConnectionRouter::~SumoConnectionRouter() {
    for (int i = 0; i < connections.size(); i++) {
        if (handlesPartition(i)) {
            delete connections[i];
        }
    }
}

void SumoConnectionRouter::connectToPartition(int partId) {
    int port = partitionPorts[partId];
    if (port < 0) {
        throw std::invalid_argument("Router " + std::to_string(ownerId) + " | Cannot connect to partition " + std::to_string(partId) + ", not a neighbor!");
    }
    auto connection = *connections[partId];
    try {
        printf("Router %d | Connecting to partition %d, port %d\n", ownerId, partId, port);
        connection.connect(host, port);
        printf("Router %d | Connected to partition %d\n", ownerId, partId);
    } catch(std::exception& e) {
        std::stringstream msg;
        msg << "Router " << ownerId << " | Exception in connecting to TraCI API at part " << partId << ": " << e.what() << std::endl;
        msg << getStackTrace() << std::endl;
        std::cerr << msg.str();
        std::exit(-10);
    }
}

void SumoConnectionRouter::connectAll() {
    for (int partId : handledPartitions) {
        connectToPartition(partId);
    }
}

void SumoConnectionRouter::closePartition(int partId) {
    int port = partitionPorts[partId];
    if (port < 0) {
        throw std::invalid_argument("Router " + std::to_string(ownerId) + " | Cannot close partition " + std::to_string(partId) + ", not a neighbor!");
    }
    auto connection = *connections[partId];
    try {
        connection.close();
    } catch(std::exception& e) {
        std::stringstream msg;
        msg << "Router " << ownerId << " | Exception in closing TraCI API to partition " << partId <<": " << e.what() << std::endl;
        msg << getStackTrace() << std::endl;
        std::cerr << msg.str();
    }
}

void SumoConnectionRouter::closeAll() {
    for (int partId : handledPartitions) {
        closePartition(partId);
    }
}

bool SumoConnectionRouter::handlesPartition(int partId) {
    return partitionPorts[partId] >= 0;
}

#define SUMO_ROUTER_ARGCHECKS(partId) if (partId == ROUTER_OWNER) partId = ownerId;\
    if (!handlesPartition(partId)) {\
        throw std::invalid_argument("Router " + std::to_string(ownerId) + " does not have as neighbor partition " + std::to_string(partId));\
    }

/*
Connection methods
Call the connection on the specified partition, or default to owner
if -1 is passed (aka ROUTER_OWNER)
*/

void SumoConnectionRouter::addVehicle(const std::string& vehID, const std::string& routeID, const std::string& typeID,
 const std::string& laneInd, const std::string& depPos, const std::string& speed, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    connections[partId]->vehicle.add(vehID, routeID, typeID, "-1", laneInd, depPos, speed);
}

void SumoConnectionRouter::moveTo(const std::string& vehID, const std::string& laneID, double pos, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    connections[partId]->vehicle.moveTo(vehID, laneID, pos);
}

std::vector<std::string> SumoConnectionRouter::getRouteEdges(const std::string& routeID, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->route.getEdges(routeID);
}

std::vector<std::string> SumoConnectionRouter::getEdgeVehicles(const std::string& edgeID, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->edge.getLastStepVehicleIDs(edgeID);
}

void SumoConnectionRouter::slowDown(const std::string& vehID, double speed, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    connections[partId]->vehicle.slowDown(vehID, speed, connections[partId]->simulation.getDeltaT());
}

std::string SumoConnectionRouter::getVehicleRouteId(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getRouteID(vehicleId);
}

float SumoConnectionRouter::getVehicleSpeed(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getSpeed(vehicleId);
}

std::string SumoConnectionRouter::getVehicleType(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getTypeID(vehicleId);
}

int SumoConnectionRouter::getVehicleLaneIndex(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getLaneIndex(vehicleId);
}

std::string SumoConnectionRouter::getVehicleLaneId(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getLaneID(vehicleId);
}

double SumoConnectionRouter::getVehicleLanePosition(std::string vehicleId, int partId) {
    SUMO_ROUTER_ARGCHECKS(partId);
    return connections[partId]->vehicle.getLanePosition(vehicleId);
}

// Only on router owner

float SumoConnectionRouter::getSimulationTime() {
    if (ownerId == -1) throw std::runtime_error("Router with no owner tried getting sim time");

    return connections[ownerId]->simulation.getTime();
}

void SumoConnectionRouter::simulationStep() {
    if (ownerId == -1) throw std::runtime_error("Router with no owner tried getting sim time");

    connections[ownerId]->simulationStep();
}
