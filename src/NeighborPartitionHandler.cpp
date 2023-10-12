#include "NeighborPartitionHandler.hpp"
#include <zmq.hpp>
#include <thread>
#include <string>

#include "utils.hpp"
#include "PartitionManager.hpp"

using namespace std;

NeighborPartitionHandler::NeighborPartitionHandler(PartitionManager& owner, int clientId, zmq::context_t& zctx) :
    owner(owner),
    clientId(clientId),
    socketUri(PartitionEdgesStub::getIpcSocketName(owner.getArgs().dataDir, clientId, owner.getId())),
    listening(false),
    stop_(false),
    term(false)
{
    socket = zmq::socket_t{zctx, zmq::socket_type::rep};
}

void NeighborPartitionHandler::start() {
    socket.bind(socketUri);
    listenThread = thread(&NeighborPartitionHandler::listenThreadLogic, this);
}

void NeighborPartitionHandler::stop() {
    term = true;
    stop_ = true;
}

void NeighborPartitionHandler::listenOn() {
    listening = true;
}

void NeighborPartitionHandler::listenOff() {
    stop_ = true;
}


void NeighborPartitionHandler::listenCheck() {
    zmq::message_t request;
    auto result = socket.recv(request, zmq::recv_flags::none);

    // Read int representing operations to call from the message
    int opcode;
    std::memcpy(&opcode, request.data(), sizeof(int));
    auto operation = static_cast<PartitionEdgesStub::Operations>(opcode);

    bool alreadyReplied = false;

    switch(operation) {
        case PartitionEdgesStub::GET_EDGE_VEHICLES:
        alreadyReplied = handleGetEdgeVehicles(request);
        break;
        case PartitionEdgesStub::SET_VEHICLE_SPEED:
        alreadyReplied = handleSetVehicleSpeed(request);
        break;
        case PartitionEdgesStub::ADD_VEHICLE:
        alreadyReplied = handleAddVehicle(request);
        break;
    }

    if (!alreadyReplied) {
        socket.send(zmq::str_buffer("ok"));
    }
}

void NeighborPartitionHandler::listenThreadLogic() {
    while(!term) {
        if (listening) {
            // Clear queues from previous listening loop
            addVehicleQueue.clear();
            setSpeedQueue.clear();
            while(!stop_) {
                listenCheck();
            }
            listening = false;
        } else {
            // Add some sort of semaphore
        }
    }
}

bool NeighborPartitionHandler::handleGetEdgeVehicles(zmq::message_t& request) {
    string edgeId(
        static_cast<char*>(request.data()) + sizeof(int), 
        static_cast<char*>(request.data()) + request.size()
    );

    std::vector<std::string> edgeVehicles = owner.getEdgeVehicles(edgeId);
    auto reply = createMessageWithStrings(edgeVehicles);
    socket.send(reply, zmq::send_flags::none);
    return true;
}

bool NeighborPartitionHandler::handleSetVehicleSpeed(zmq::message_t& request) {
    double speed;
    std::memcpy(&speed, static_cast<double*>(request.data()) + sizeof(int), sizeof(double));
    string veh(
        static_cast<char*>(request.data()) + sizeof(double) + sizeof(int), 
        static_cast<char*>(request.data()) + request.size()
    );
    
    setSpeedQueue.push_back({veh, speed});

    return false;
}

bool NeighborPartitionHandler::handleAddVehicle(zmq::message_t& request) {
    int laneIndex;
    double lanePos, speed;
    std::memcpy(&laneIndex, static_cast<int*>(request.data()) + sizeof(int), sizeof(int));
    std::memcpy(&lanePos, static_cast<double*>(request.data()) + sizeof(int) * 2, sizeof(double));
    std::memcpy(&speed, static_cast<double*>(request.data()) + sizeof(int) * 2 + sizeof(double), sizeof(double));

    int stringsOffset = sizeof(int) * 2 + sizeof(double) * 2;
    auto strings = readStringsFromMessage(request, stringsOffset);

    addVehicleQueue.push_back({
        strings[0],
        strings[1],
        strings[2],
        strings[3],
        laneIndex,
        lanePos,
        speed
    });

    return false;
}

bool NeighborPartitionHandler::handleStepEnd(zmq::message_t& request) {
    return false;
}