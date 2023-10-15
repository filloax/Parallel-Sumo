/**
PartitionEdgesStub.hpp

Connects to neighboring partitions, running operations remotely on them.

Author: Filippo Lenzi
*/

#pragma once

#include <zmq.hpp>

class PartitionEdgesStub;

#include "PartitionManager.hpp"

class PartitionEdgesStub {
private:
    Args& args;
    partId_t ownerId;
    partId_t id;
    bool connected;
    std::string socketUri;
    zmq::socket_t socket;
public:
    enum Operations {
        GET_EDGE_VEHICLES,
        SET_VEHICLE_SPEED,
        ADD_VEHICLE,
    };

    static std::string getSocketName(std::string directory, partId_t from, partId_t to);

    PartitionEdgesStub(partId_t ownerId, partId_t targetId, zmq::context_t& zcontext, Args& args);
    ~PartitionEdgesStub();

    void setVehicleSpeed(const std::string& vehId, double speed);
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );
    void connect();
    void disconnect();
};