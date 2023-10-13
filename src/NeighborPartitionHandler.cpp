#include "NeighborPartitionHandler.hpp"

#include <zmq.hpp>
#include <thread>
#include <string>
#include <mutex>

#include "utils.hpp"
#include "PartitionManager.hpp"

using namespace std;

#ifdef USING_WIN
    #define z_transport tcp
#else
    #define z_transport ipc
#endif

string getSocketUri(string dataDir, int clientId, int ownerId) {
    #if z_transport == ipc
        return PartitionEdgesStub::getIpcSocketName(dataDir, clientId, ownerId);
    #else
        printf("TPC transport not yet implemented!\n");
        exit(-5);
    #endif
}

NeighborPartitionHandler::NeighborPartitionHandler(PartitionManager& owner, int clientId, zmq::context_t& zctx) :
    owner(owner),
    clientId(clientId),
    socketUri(getSocketUri(owner.getArgs().dataDir, clientId, owner.getId())),
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
            while(!stop_) {
                listenCheck();
            }
            listening = false;
        } else {
            // TODO: Add some sort of semaphore
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
    
    // lock to be 100% sure with the applying of operations later
    operationsBufferLock.lock();
    setSpeedQueue.push_back({veh, speed});
    operationsBufferLock.unlock();

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

    // lock to be 100% sure with the applying of operations later
    operationsBufferLock.lock();
    addVehicleQueue.push_back({
        strings[0],
        strings[1],
        strings[2],
        strings[3],
        laneIndex,
        lanePos,
        speed
    });
    operationsBufferLock.unlock();

    return false;
}

bool NeighborPartitionHandler::handleStepEnd(zmq::message_t& request) {
    return false;
}

// Execute the queued operations that other partitions ran
void NeighborPartitionHandler::applyMutableOperations() {
    bool wasListening = listening;
    if (listening) {
        listenOff();
    }
    // Lock to avoid other threads adding more operations in the meantime
    // if it was inbetween one of them when we set this to stop
    operationsBufferLock.lock();

    for (auto addVeh : addVehicleQueue) {
        owner.addVehicle(
            addVeh.vehId, addVeh.routeId, addVeh.vehType, 
            addVeh.laneId, addVeh.laneIndex, addVeh.lanePos, addVeh.speed
        );
    }

    for (auto setSpeed : setSpeedQueue) {
        owner.setVehicleSpeed(setSpeed.vehId, setSpeed.speed);
    }
    
    addVehicleQueue.clear();
    setSpeedQueue.clear();

    operationsBufferLock.unlock();

    if (wasListening) listenOn();
}