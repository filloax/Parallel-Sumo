/**
NeighborPartitionHandler.cpp

Handles incoming messages from neighboring partitions, either responding with state
or queueing modifying operations (like adding vehicles).

Author: Filippo Lenzi
*/

#include "NeighborPartitionHandler.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <libsumo/TraCIDefs.h>
#include <sstream>
#include <zmq.hpp>
#include <thread>
#include <string>
#include <mutex>

#include "messagingShared.hpp"
#include "src/ContextPool.hpp"
#include "utils.hpp"
#include "PartitionManager.hpp"

using namespace std;
using namespace psumo;

NeighborPartitionHandler::NeighborPartitionHandler(PartitionManager& owner, int clientId) :
    owner(owner),
    clientId(clientId),
    socketUri(psumo::getSocketName(owner.getArgs().dataDir, clientId, owner.getId(), owner.getNumThreads())),
    listening(false),
    stop_(false),
    term(false),
    threadWaiting(false),
    zcontext(ContextPool::newContext(1))
{
    socket = zmq::socket_t{zcontext, zmq::socket_type::rep};
    socket.set(zmq::sockopt::linger, 0 );

    controlSocketMain = zmq::socket_t{zcontext, zmq::socket_type::pair};
    controlSocketMain.set(zmq::sockopt::linger, 0 );
    controlSocketThread = zmq::socket_t{zcontext, zmq::socket_type::pair};
    controlSocketThread.set(zmq::sockopt::linger, 0 );
}

const int DISCONNECT_CONTEXT_TERMINATED_ERR = 156384765;
    
NeighborPartitionHandler::~NeighborPartitionHandler() {
    #ifndef NDEBUG
        logerr("Destroying...\n");
        printStackTrace();
    #endif
    stop();
    try {
        socket.close();
    } catch (zmq::error_t& e) {
        // If the context is already terminated, then not a problem if it didn't disconnect
        if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
            stringstream msg;
            logerr("Error in disconnecting socket during destructor: {}/{}\n", e.what(), e.num());
        }
    }
}

void NeighborPartitionHandler::start() {
    try {
        socket.bind(socketUri);
    } catch (zmq::error_t& e) {
        logerr("ZMQ error in binding socket {} to {}: {}/{}\n", clientId, socketUri, e.what(), e.num());
        exit(EXIT_FAILURE);
    }
    try {
        stringstream uris;
        uris << "inproc://nb" << clientId << "-" << owner.getId();
        auto uri = uris.str();
        controlSocketThread.bind(uri);
        controlSocketMain.connect(uri);
    } catch (zmq::error_t& e) {
        logerr("ZMQ error in binding inproc sockets: {}/{}\n", e.what(), e.num());
        exit(EXIT_FAILURE);
    }

    listenThread = thread(&NeighborPartitionHandler::listenThreadLogic, this);
}

void NeighborPartitionHandler::stop() {
    log("Terminating when possible\n");
    term = true;
    stop_ = true;
    controlSocketMain.send(zmq::str_buffer("stop"), zmq::send_flags::none);
}

void NeighborPartitionHandler::join() {
    listenThread.join();
}

void NeighborPartitionHandler::listenOn() {
    log("Turning listen on\n");
    lock_guard<mutex> lock(secondThreadSignalLock);
    listening = true;
    stop_ = false;
    if (threadWaiting) secondThreadCondition.notify_one();
}

void NeighborPartitionHandler::listenOff() {
    log("Listen off when possible\n");
    stop_ = true;
}


void NeighborPartitionHandler::listenCheck() {
    // Wait for the first message between the partition socket and the thread socket,
    // thread socket meaning work should be interrupted (partition stopped)
    zmq::pollitem_t pollitems[] = { 
        { socket, 0, ZMQ_POLLIN, 0 }, 
        { controlSocketThread, 0, ZMQ_POLLIN, 0 } 
    };

    log("Waiting for requests...\n");
    int rc = zmq::poll(pollitems, 2);

    if (rc == -1) {
        logerr("[WARN] zmq::poll interrupted\n");
        return;
    }
    if (pollitems[1].revents & ZMQ_POLLIN) {
        // Received a message on the control socket; quit
        log("Control socket message received, stopping listen\n");
        return;
    }
    
    zmq::message_t request;
    if (pollitems[0].revents & ZMQ_POLLIN) {
        zmq::message_t message;
        auto _ = socket.recv(request, zmq::recv_flags::none);
    }

    // Read int representing operations to call from the message
    int opcode;
    std::memcpy(&opcode, request.data(), sizeof(int));
    auto operation = static_cast<PartitionEdgesStub::Operations>(opcode);

    log("Received request for opcode {}\n", opcode);

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
        log("Sending generic reply for opcode {}\n", opcode);
        socket.send(zmq::str_buffer("ok"));
    }
}

void NeighborPartitionHandler::listenThreadLogic() {
    #ifndef NDEBUG
    try {
    #endif

    while(!term) {
        if (listening) {
            log("Starting listen loop...\n");
            while(!stop_) {
                listenCheck();
            }
            listening = false;
            log("Stopped listen loop\n");
        } else {
            threadWaiting = true;
            unique_lock<mutex> lock(secondThreadSignalLock);
            secondThreadCondition.wait(lock, [this] { return listening; });
            threadWaiting = false;
        }
    }

    #ifndef NDEBUG
    } catch(libsumo::TraCIException& e) {
        logerr("SUMO error: {}\n=== {} QUITTING ===\n", e.what(), getPid());
        exit(EXIT_FAILURE);
    } catch(zmq::error_t& e) {
        logerr("SUMO error: {}/{}\n=== {} QUITTING ===\n", e.what(), e.num(), getPid());
        exit(EXIT_FAILURE);
    }
    #endif
}

bool NeighborPartitionHandler::handleGetEdgeVehicles(zmq::message_t& request) {
    string edgeId(
        static_cast<char*>(request.data()) + sizeof(int), 
        static_cast<char*>(request.data()) + request.size()
    );

    log("Received getEdgeVehicles({})\n", edgeId.c_str());

    vector<string> edgeVehicles = owner.getEdgeVehicles(edgeId);
    auto reply = createMessageWithStrings(edgeVehicles);
    log("Sending reply to getEdgeVehicles({})\n", edgeId.c_str());

    stringstream ss;
    ss << "\tPart. handler " << clientId << "->" << owner.getId() << " | Replying with [";
    printVector(edgeVehicles, "", ", ", false, ss);
    ss << "]" << endl;
    cout << ss.str();

    socket.send(reply, zmq::send_flags::none);
    return true;
}

bool NeighborPartitionHandler::handleSetVehicleSpeed(zmq::message_t& request) {
    double speed;
    const char* data = static_cast<char*>(request.data());
    std::memcpy(&speed, data + sizeof(int), sizeof(double));
    string veh(
        data + sizeof(double) + sizeof(int), 
        data + request.size()
    );
    
    log("Queueing setVehicleSpeed ({}, {})\n", veh.c_str(), speed);

    // lock to be 100% sure with the applying of operations later
    operationsBufferLock.lock();
    // TODO: add retry later on fail
    bool success = setSpeedQueue.append({veh, speed});
    operationsBufferLock.unlock();

    return false;
}

bool NeighborPartitionHandler::handleAddVehicle(zmq::message_t& request) {
    int laneIndex;
    double lanePos, speed;
    const char* data = static_cast<char*>(request.data());
    std::memcpy(&laneIndex, data + sizeof(int), sizeof(int));
    std::memcpy(&lanePos, data + sizeof(int) * 2, sizeof(double));
    std::memcpy(&speed, data + sizeof(int) * 2 + sizeof(double), sizeof(double));

    int stringsOffset = sizeof(int) * 2 + sizeof(double) * 2;
    auto strings = readStringsFromMessage(request, stringsOffset);

    log("Queueing addVehicle ({}, ...)\n", strings[0].c_str());

    // lock to be 100% sure with the applying of operations later
    operationsBufferLock.lock();
    // TODO: add retry later on fail
    bool success = addVehicleQueue.append({
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
    int num = addVehicleQueue.currentSize + setSpeedQueue.currentSize;
    if (num > 0) {
        log("Applying mutable operations (has {})\n", num);
        bool wasListening = listening;
        if (listening) {
            log("Listen off to apply mutable operations\n");
            listenOff();
        }
        // Lock to avoid other threads adding more operations in the meantime
        // if it was inbetween one of them when we set this to stop
        operationsBufferLock.lock();

        log("Mutable ops passed lock\n");

        for (int i = 0; i < addVehicleQueue.currentSize; i++) {
            auto addVeh = addVehicleQueue.queue[i];
            owner.addVehicle(
                addVeh.vehId, addVeh.routeId, addVeh.vehType, 
                addVeh.laneId, addVeh.laneIndex, addVeh.lanePos, addVeh.speed
            );
        }

        for (int i = 0; i < setSpeedQueue.currentSize; i++) {
            auto setSpeed = setSpeedQueue.queue[i];
            owner.setVehicleSpeed(setSpeed.vehId, setSpeed.speed);
        }
        
        addVehicleQueue.clear();
        setSpeedQueue.clear();

        operationsBufferLock.unlock();

        if (wasListening) listenOn();

        log("Done applying mutable operations (was listening: {})\n", wasListening);
    }
}

template<typename... _Args > 
inline void NeighborPartitionHandler::log(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "\tPart. handler " << clientId << "->" << owner.getId() 
        << " [" << this_thread::get_id() << "]"
        << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cout << msg.str();
}

template<typename... _Args>
inline void NeighborPartitionHandler::logerr(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "\tPart. handler " << clientId << "->" << owner.getId()
        << " [" << this_thread::get_id() << "]"
        << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cerr << msg.str();
}