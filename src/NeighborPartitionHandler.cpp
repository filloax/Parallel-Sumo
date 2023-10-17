/**
NeighborPartitionHandler.cpp

Handles incoming messages from neighboring partitions, either responding with state
or queueing modifying operations (like adding vehicles).

Author: Filippo Lenzi
*/

#include "NeighborPartitionHandler.hpp"

#include <condition_variable>
#include <cstdlib>
#include <libsumo/TraCIDefs.h>
#include <sstream>
#include <zmq.hpp>
#include <thread>
#include <string>
#include <mutex>

#include "messagingShared.hpp"
#include "utils.hpp"
#include "PartitionManager.hpp"

using namespace std;

NeighborPartitionHandler::NeighborPartitionHandler(PartitionManager& owner, int clientId) :
    owner(owner),
    clientId(clientId),
    socketUri(psumo::getSocketName(owner.getArgs().dataDir, clientId, owner.getId(), owner.getNumThreads())),
    listening(false),
    stop_(false),
    term(false)
{
    zcontext = new zmq::context_t{1};
    socket = zmq::socket_t{*zcontext, zmq::socket_type::rep};
    socket.set(zmq::sockopt::linger, 0 );
}

const int DISCONNECT_CONTEXT_TERMINATED_ERR = 156384765;

NeighborPartitionHandler::~NeighborPartitionHandler() {
    stop();
    try {
        socket.close();
    } catch (zmq::error_t& e) {
        // If the context is already terminated, then not a problem if it didn't disconnect
        if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
            stringstream msg;
            msg << "Part. handler " << owner.getId() << ":" << clientId << " | "
                << "Error in disconnecting socket during destructor:" << e.what() << "/" << e.num()
                << endl;
            cerr << msg.str();
        }
    }
    try {
        zcontext->shutdown();
        zcontext->close();
    } catch (zmq::error_t& e) {
        stringstream msg;
        msg << "Part. handler " << owner.getId() << ":" << clientId << " | "
            << "Error in closing context during destructor:" << e.what() << "/" << e.num()
            << endl;
        cerr << msg.str();
    }
    #ifndef NDEBUG
        printf("Part. handler %d->%d | Deleting context\n", clientId, owner.getId());
    #endif
    delete zcontext;
}

void NeighborPartitionHandler::start() {
    try {
        socket.bind(socketUri);
    } catch (zmq::error_t& e) {
      stringstream msg;
      msg << "Part. Handler "<< owner.getId() <<" | ZMQ error in binding socket " << clientId << " to '" << socketUri
        << "': " << e.what() << "/" << e.num() << endl;
      cerr << msg.str();
      exit(-10);
    }

    listenThread = thread(&NeighborPartitionHandler::listenThreadLogic, this);
}

void NeighborPartitionHandler::stop() {
    term = true;
    stop_ = true;
}

void NeighborPartitionHandler::join() {
    listenThread.join();
}

void NeighborPartitionHandler::listenOn() {
    lock_guard<mutex> lock(secondThreadSignalLock);
    listening = true;
    secondThreadCondition.notify_one();
}

void NeighborPartitionHandler::listenOff() {
    stop_ = true;
}


void NeighborPartitionHandler::listenCheck() {
    zmq::message_t request;
    printf("\tPart. handler %d->%d | Waiting for requests...\n", clientId, owner.getId());
    auto result = socket.recv(request, zmq::recv_flags::none);

    // Read int representing operations to call from the message
    int opcode;
    std::memcpy(&opcode, request.data(), sizeof(int));
    auto operation = static_cast<PartitionEdgesStub::Operations>(opcode);

    printf("\tPart. handler %d->%d | Received request for opcode %d\n", clientId, owner.getId(), opcode);

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
        printf("\tPart. handler %d->%d | Sending generic reply for opcode %d\n", clientId, owner.getId(), opcode);
        socket.send(zmq::str_buffer("ok"));
    }
}

void NeighborPartitionHandler::listenThreadLogic() {
    #ifndef NDEBUG
    try {
    #endif

    while(!term) {
        if (listening) {
            printf("\tPart. handler %d->%d | Starting listen loop...\n", clientId, owner.getId());
            while(!stop_) {
                listenCheck();
            }
            listening = false;
            printf("\tPart. handler %d->%d | Stopped listen loop\n", clientId, owner.getId());
        } else {
            unique_lock<mutex> lock(secondThreadSignalLock);
            secondThreadCondition.wait(lock, [this] { return listening; });
        }
    }

    #ifndef NDEBUG
    } catch(libsumo::TraCIException& e) {
        stringstream msg;
        msg << "Part. handler " << clientId << "->" << owner.getId() 
            << " | SUMO error: " << e.what() << endl << "=== " << getPid() << " QUITTING ===" << endl;
        cerr << msg.str();
        exit(EXIT_FAILURE);
    } catch(zmq::error_t& e) {
        stringstream msg;
        msg << "Part. handler " << clientId << "->" << owner.getId() 
            << " | ZMQ error: " << e.what() << "/" << e.num()
            << endl << "=== " << getPid() << " QUITTING ===" << endl;
        cerr << msg.str();
        exit(EXIT_FAILURE);
    }
    #endif
}

bool NeighborPartitionHandler::handleGetEdgeVehicles(zmq::message_t& request) {
    string edgeId(
        static_cast<char*>(request.data()) + sizeof(int), 
        static_cast<char*>(request.data()) + request.size()
    );

    printf("\tPart. handler %d->%d | Received getEdgeVehicles(%s)\n", clientId, owner.getId(), edgeId.c_str());

    vector<string> edgeVehicles = owner.getEdgeVehicles(edgeId);
    auto reply = createMessageWithStrings(edgeVehicles);
    printf("\tPart. handler %d->%d | Sending reply to getEdgeVehicles(%s)\n", clientId, owner.getId(), edgeId.c_str());
    stringstream ss;
    ss << "\tPart handler " << clientId << "->" << owner.getId() << " | Replying with [";
    printVector(edgeVehicles, "", ", ", false, ss);
    ss << "]" << endl;
    cout << ss.str();

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
    
    printf("\tPart. handler %d->%d | Queueing setVehicleSpeed (%s, %f)\n", clientId, owner.getId(), veh.c_str(), speed);

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

    printf("\tPart. handler %d->%d | Queueing addVehicle (%s, ...)\n", clientId, owner.getId(), strings[0].c_str());

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