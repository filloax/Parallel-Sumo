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
#include "src/PartitionEdgesStub.hpp"
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
    socket = makeSocket(zcontext, zmq::socket_type::rep);
    controlSocketMain = makeSocket(zcontext, zmq::socket_type::pair);
    controlSocketThread = makeSocket(zcontext, zmq::socket_type::pair);
}

const int DISCONNECT_CONTEXT_TERMINATED_ERR = 156384765;
    
NeighborPartitionHandler::~NeighborPartitionHandler() {
    stop();
    delete socket;
    delete controlSocketMain;
    delete controlSocketThread;
}

void NeighborPartitionHandler::start() {
    try {
        bind(*socket, socketUri);
    } catch (zmq::error_t& e) {
        logerr("ZMQ error in binding socket {} to {}: {}/{}\n", clientId, socketUri, e.what(), e.num());
        exit(EXIT_FAILURE);
    }
    try {
        stringstream uris;
        uris << "inproc://nb" << clientId << "-" << owner.getId();
        auto uri = uris.str();
        bind(*controlSocketThread, uri);
        connect(*controlSocketMain, uri);
    } catch (zmq::error_t& e) {
        logerr("ZMQ error in binding inproc sockets: {}/{}\n", e.what(), e.num());
        exit(EXIT_FAILURE);
    }

    listenThread = thread(&NeighborPartitionHandler::listenThreadLogic, this);
}

void NeighborPartitionHandler::stop() {
    if (stop_) return;
    
    log("Terminating...\n");
    term = true;
    stop_ = true;
    controlSocketMain->send(zmq::str_buffer("stop"), zmq::send_flags::none);

    join();
    
    try {
        close(*socket);
    } catch (zmq::error_t& e) {
        // If the context is already terminated, then not a problem if it didn't disconnect
        if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
            stringstream msg;
            logerr("Error in disconnecting socket during destructor: {}/{}\n", e.what(), e.num());
        }
    }
    try {
        close(*controlSocketMain);
    } catch (zmq::error_t& e) {
        // If the context is already terminated, then not a problem if it didn't disconnect
        if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
            stringstream msg;
            logerr("Error in disconnecting socket during destructor: {}/{}\n", e.what(), e.num());
        }
    }
    try {
        close(*controlSocketThread);
    } catch (zmq::error_t& e) {
        // If the context is already terminated, then not a problem if it didn't disconnect
        if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
            stringstream msg;
            logerr("Error in disconnecting socket during destructor: {}/{}\n", e.what(), e.num());
        }
    }
}

void NeighborPartitionHandler::join() {
    if (listening) {
        listenThread.join();
        log("Listen thread joined\n");
    } else {
        log("Listen thread already joined\n");
    }
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
        { castPollSocket(*socket), 0, ZMQ_POLLIN, 0 }, 
        { castPollSocket(*controlSocketThread), 0, ZMQ_POLLIN, 0 } 
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
        auto _ = socket->recv(request, zmq::recv_flags::none);
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
        case PartitionEdgesStub::HAS_VEHICLE:
            alreadyReplied = handleHasVehicle(request);
            break;
        case PartitionEdgesStub::HAS_VEHICLE_IN_EDGE:
            alreadyReplied = handleHasVehicleInEdge(request);
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
        socket->send(zmq::str_buffer("ok"));
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
        logerr("ZMQ error: {}/{}\n=== {} QUITTING ===\n", e.what(), e.num(), getPid());
        exit(EXIT_FAILURE);
    }
    #endif
}

bool NeighborPartitionHandler::handleGetEdgeVehicles(zmq::message_t& request) {
    auto data = static_cast<char*>(request.data());
    string edgeId(
        data + sizeof(int), 
        data + request.size() - 1
    );

    log("Received getEdgeVehicles({})\n", edgeId);

    vector<string> edgeVehicles = owner.getEdgeVehicles(edgeId);
    auto reply = createMessageWithStrings(edgeVehicles);
    log("Sending reply to getEdgeVehicles({})\n", edgeId);

    if (owner.getArgs().verbose) {
        stringstream ss;
        ss << "\tPart. handler " << clientId << "->" << owner.getId() << " | Replying with [";
        printVector(edgeVehicles, "", ", ", false, ss);
        ss << "]" << endl;
        cout << ss.str();
    }

    socket->send(reply, zmq::send_flags::none);
    return true;
}

bool NeighborPartitionHandler::handleHasVehicle(zmq::message_t& request) {
    auto data = static_cast<char*>(request.data());
    string vehId(
        data + sizeof(int), 
        data + request.size() - 1
    );

    log("Received hasVehicle({}) [{}]\n", vehId, request.size());

    bool has = owner.hasVehicle(vehId);

    zmq::message_t reply(sizeof(bool));
    std::memcpy(static_cast<char*>(reply.data()), &has, sizeof(bool));
    log("Sending reply to hasVehicle({}): {}\n", vehId, has);

    socket->send(reply, zmq::send_flags::none);
    return true;
}

bool NeighborPartitionHandler::handleHasVehicleInEdge(zmq::message_t& request) {
    auto strings = readStringsFromMessage(request, sizeof(int));
    string& vehId = strings[0];
    string& edgeId = strings[1];

    log("Received hasVehicleInEdge({}, {})\n", vehId, edgeId);

    bool has = owner.hasVehicleInEdge(vehId, edgeId);

    zmq::message_t reply(sizeof(bool));
    std::memcpy(static_cast<char*>(reply.data()), &has, sizeof(bool));
    log("Sending reply to hasVehicleInEdge({}, {}): {}\n", vehId, edgeId, has);

    socket->send(reply, zmq::send_flags::none);
    return true;
}

bool NeighborPartitionHandler::handleSetVehicleSpeed(zmq::message_t& request) {
    double speed;
    const char* data = static_cast<char*>(request.data());
    std::memcpy(&speed, data + sizeof(int), sizeof(double));
    string veh(
        data + sizeof(double) + sizeof(int), 
        data + request.size() - 1
    );
    
    log("Queueing setVehicleSpeed ({}, {})\n", veh, speed);

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
    std::memcpy(&lanePos,   data + sizeof(int) * 2, sizeof(double));
    std::memcpy(&speed,     data + sizeof(int) * 2 + sizeof(double), sizeof(double));

    int stringsOffset = sizeof(int) * 2 + sizeof(double) * 2;
    auto strings = readStringsFromMessage(request, stringsOffset);

    log("Queueing addVehicle (addVehicle({}, {}, {}, {}, {}, {})\n",
        strings[0], strings[1], strings[2], strings[3], laneIndex, lanePos, speed);

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

// Execute the queued operations that other partitions ran
void NeighborPartitionHandler::applyMutableOperations() {
    int num = addVehicleQueue.currentSize + setSpeedQueue.currentSize;
    if (num > 0) {
        log("Applying modifying operations (has {} addVehicle, {} setSpeed)\n", addVehicleQueue.currentSize, setSpeedQueue.currentSize);
        bool wasListening = listening;
        if (listening) {
            log("Listen off to apply modifying operations\n");
            listenOff();
        }
        // Lock to avoid other threads adding more operations in the meantime
        // if it was inbetween one of them when we set this to stop
        operationsBufferLock.lock();

        log("Modifying ops passed lock\n");

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

        log("Done applying modifying operations (was listening: {})\n", wasListening);
    }
}

template<typename... _Args > 
void NeighborPartitionHandler::log(std::format_string<_Args...> format, _Args&&... args_) {
    if (!owner.getArgs().verbose) return;

    std::stringstream msg;
    msg << "\tPart. handler " << clientId << "->" << owner.getId() 
        << " [" << this_thread::get_id() << "]"
        << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args_)...
    );
    std::cout << msg.str();
}

template<typename... _Args>
void NeighborPartitionHandler::logerr(std::format_string<_Args...> format, _Args&&... args_) {
    std::stringstream msg;
    msg << "\tPart. handler " << clientId << "->" << owner.getId()
        << " [" << this_thread::get_id() << "]"
        << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args_)...
    );
    std::cerr << msg.str();
}