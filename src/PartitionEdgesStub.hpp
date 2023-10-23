/**
PartitionEdgesStub.hpp

Connects to neighboring partitions, running operations remotely on them.

Author: Filippo Lenzi
*/

#pragma once

#include <zmq.hpp>
#include <format>

class PartitionEdgesStub;

#include "PartitionManager.hpp"

using namespace psumo;

class PartitionEdgesStub {
private:
    Args& args;
    partId_t ownerId;
    partId_t id;
    bool connected;
    const std::string socketUri;
    zmq::socket_t* socket;

    template<typename... _Args > 
        void log(std::format_string<_Args...>  format, _Args&&... args);
    template<typename... _Args > 
        void logerr(std::format_string<_Args...>  format, _Args&&... args);
public:
    enum Operations {
        GET_EDGE_VEHICLES,
        HAS_VEHICLE,
        HAS_VEHICLE_IN_EDGE,
        SET_VEHICLE_SPEED,
        ADD_VEHICLE,
    };

    PartitionEdgesStub(partId_t ownerId, partId_t targetId, int numThreads, zmq::context_t& zcontext, Args& args);
    ~PartitionEdgesStub();

    // If possible, use hasVehicle and hasVehicleInEdge instead, as they
    // transfer less data
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId);
    bool hasVehicle(const std::string& vehId);
    bool hasVehicleInEdge(const std::string& vehId, const std::string& edgeId);
    void setVehicleSpeed(const std::string& vehId, double speed);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );

    void connect();
    void disconnect();
};