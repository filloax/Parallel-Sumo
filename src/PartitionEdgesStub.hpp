#pragma once

#include <zmq.hpp>

#include "PartitionManager.hpp"

class PartitionEdgesStub {
private:
    Args& args;
    partId_t ownerId;
    partId_t id;
    zmq::socket_t socket;
    zmq::context_t zcontext;
public:
    enum Operations {
        SIGNAL_STEP_END,
        GET_EDGE_VEHICLES,
        SET_VEHICLE_SPEED,
        ADD_VEHICLE,
    };

    static std::string getIpcSocketName(std::string directory, partId_t from, partId_t to);

    PartitionEdgesStub(partId_t ownerId, partId_t targetId, Args& args);

    void setVehicleSpeed(const std::string& vehId, double speed);
    std::vector<std::string> getEdgeVehicles(const std::string& edgeId);
    void addVehicle(
        const std::string& vehId, const std::string& routeId, const std::string& vehType,
        const std::string& laneId, int laneIndex, double lanePos, double speed
    );
    void connect();

    // Step sync
    void signalStepEnd();
};