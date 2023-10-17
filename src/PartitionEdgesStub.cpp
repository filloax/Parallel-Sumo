/**
PartitionEdgesStub.cpp

Connects to neighboring partitions, running operations remotely on them.

Author: Filippo Lenzi
*/

#include "PartitionEdgesStub.hpp"
#include "PartitionManager.hpp"
#include "utils.hpp"
#include "messagingShared.hpp"

#include <cstddef>
#include <cstring>
#include <sstream>
#include <string>
#include <zmq.hpp>

using namespace std;

#ifdef USING_WIN
#define z_transport tcp
#else
#define z_transport ipc
#endif

// Current implementation of messaging uses constant
// size to instantiate messages
#define SUMO_ID_SIZE 256

PartitionEdgesStub::PartitionEdgesStub(partId_t ownerId, partId_t targetId, int numThreads, zmq::context_t& zcontext, Args& args):
    ownerId(ownerId),
    id(targetId),
    connected(false),
    socketUri(psumo::getSocketName(args.dataDir, ownerId, targetId, numThreads)),
    args(args)
{
    socket = zmq::socket_t{zcontext, zmq::socket_type::req};
    socket.set(zmq::sockopt::linger, 0 );
}

const int DISCONNECT_CONTEXT_TERMINATED_ERR = 156384765;

PartitionEdgesStub::~PartitionEdgesStub() {
    if (connected) {
        try {
            socket.close();
        } catch (zmq::error_t& e) {
            // If the context is already terminated, then not a problem if it didn't disconnect
            if (e.num() != DISCONNECT_CONTEXT_TERMINATED_ERR) {
                logerr("Error in disconnecting socket during destructor: {}/{}\n", e.what(), e.num());
            }
        }
    }
}

void PartitionEdgesStub::connect() {
    socket.connect(socketUri);
    connected = true;
}

void PartitionEdgesStub::disconnect() {
    connected = false;
    socket.close();
}

std::vector<std::string> PartitionEdgesStub::getEdgeVehicles(const std::string& edgeId) {
    int opcode = Operations::GET_EDGE_VEHICLES;

    log("Preparing getEdge\n");

    // As usual, add +1 to string size to include NULL endpoint
    int msgLength = sizeof(int) + edgeId.size() + 1;
    zmq::message_t message(msgLength);

    auto data = static_cast<char*>(message.data());
    
    std::memcpy(data,                &opcode, sizeof(int));
    std::memcpy( data + sizeof(int), edgeId.data(),  edgeId.size() + 1);

    log("Sending getEdge\n");
    socket.send(message, zmq::send_flags::none);

    log("Receiving getEdge reply\n");
    zmq::message_t reply;
    auto response = socket.recv(reply);

    auto out = readStringsFromMessage(reply);

    stringstream msg;
    msg << "\tStub " << ownerId << "->"<< id << " | Received: [";
    printVector(out, "", ", ", false, msg);
    msg << "]" << endl;
    cout << msg.str();

    return out;
}

void PartitionEdgesStub::setVehicleSpeed(const string& vehId, double speed) {
    int opcode = Operations::SET_VEHICLE_SPEED;

    log("Preparing setVehicleSpeed\n");

    // As usual, add +1 to string size to include NULL endpoint
    size_t msgLength = sizeof(int) + sizeof(double) + vehId.size() + 1;
    zmq::message_t message(msgLength);

    char* data = static_cast<char*>(message.data());
    std::memcpy(data,
        &opcode, sizeof(int));
    std::memcpy( data + sizeof(int), 
        &speed,  sizeof(double));
    std::memcpy(data + sizeof(double) + sizeof(int),
        vehId.data(), vehId.size() + 1);

    log("Sending setSpeed\n");
    socket.send(message, zmq::send_flags::none);

    log("Receiving setSpeed reply\n");
    // unused reply, required by zeroMQ
    zmq::message_t reply;
    auto response = socket.recv(reply);
}

void PartitionEdgesStub::addVehicle(
    const std::string& vehId, const std::string& routeId, const std::string& vehType,
    const std::string& laneId, int laneIndex, double lanePos, double speed
) {
    int opcode = Operations::ADD_VEHICLE;

    log("Preparing addVehicle\n");

    // opcode, laneIndex, lanePos, speed
    int stringsOffset = sizeof(int) * 2 + sizeof(double) * 2;
    vector<string> strings({
        vehId, routeId, vehType, laneId
    });
    // First create calculating size from the strings, then insert the numbers
    // on the created buffer
    auto message = createMessageWithStrings(strings, stringsOffset);
    char* data = static_cast<char*>(message.data());

    std::memcpy(data,
        &opcode, sizeof(int));
    std::memcpy( data + sizeof(int), 
        &laneIndex,  sizeof(int));
    std::memcpy( data + sizeof(int) * 2, 
        &lanePos,  sizeof(double));
    std::memcpy( data + sizeof(int) * 2 + sizeof(double), 
        &speed,  sizeof(double));

    log("Sending addVehicle\n");
    socket.send(message, zmq::send_flags::none);

    log("Receiving addVehicle reply\n");
    // unused reply, required by zeroMQ
    zmq::message_t reply;
    auto response = socket.recv(reply);
}

template<typename... _Args > 
inline void PartitionEdgesStub::log(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "\tStub " << ownerId << "->" << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cout << msg.str();
}

template<typename... _Args>
inline void PartitionEdgesStub::logerr(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "\tStub " << ownerId << "->" << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cerr << msg.str();
}