/**
PartitionEdgesStub.cpp

Connects to neighboring partitions, running operations remotely on them.

Author: Filippo Lenzi
*/

#include "PartitionEdgesStub.hpp"
#include "PartitionManager.hpp"
#include "utils.hpp"
#include "messagingShared.hpp"

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
                stringstream msg;
                msg << "Part. stub " << ownerId << "->" << id << " | "
                    << "Error in disconnecting socket during destructor:" << e.what() << "/" << e.num()
                    << endl;
                cerr << msg.str();
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

#define copy_num(type, var, message, offset) std::memcpy(\
    static_cast<type*>(message.data()) + offset,\
    &var + offset,\
    sizeof(type)\
)

std::vector<std::string> PartitionEdgesStub::getEdgeVehicles(const std::string& edgeId) {
    int opcode = Operations::GET_EDGE_VEHICLES;

    // As usual, add +1 to string size to include NULL endpoint
    int msgLength = sizeof(int) + edgeId.size() + 1;
    zmq::message_t message(msgLength);
    
    copy_num(int, opcode, message, 0);
    std::memcpy(
        static_cast<char*>(message.data()) + sizeof(int),
        edgeId.data(), 
        edgeId.size() + 1
    );

    printf("\tStub %d->%d | Sending getEdge\n", ownerId, id);
    socket.send(message, zmq::send_flags::none);

    printf("\tStub %d->%d | Receiving getEdge reply\n", ownerId, id);
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

    // As usual, add +1 to string size to include NULL endpoint
    int msgLength = sizeof(int) + sizeof(double) + vehId.size() + 1;
    zmq::message_t message(msgLength);

    copy_num(int, opcode, message, 0);
    copy_num(double, speed, message, sizeof(int));
    std::memcpy(
        static_cast<char*>(message.data()) + sizeof(double) + sizeof(int),
        vehId.data(), 
        vehId.size() + 1
    );

    printf("\tStub %d->%d | Sending setSpeed\n", ownerId, id);
    socket.send(message, zmq::send_flags::none);

    printf("\tStub %d->%d | Receiving setSpeed reply\n", ownerId, id);
    // unused reply, required by zeroMQ
    zmq::message_t reply;
    auto response = socket.recv(reply);
}

void PartitionEdgesStub::addVehicle(
    const std::string& vehId, const std::string& routeId, const std::string& vehType,
    const std::string& laneId, int laneIndex, double lanePos, double speed
) {
    int opcode = Operations::ADD_VEHICLE;

    // opcode, laneIndex, lanePos, speed
    int stringsOffset = sizeof(int) * 2 + sizeof(double) * 2;
    vector<string> strings({
        vehId, routeId, vehType, laneId
    });
    // First create calculating size from the strings, then insert the numbers
    // on the created buffer
    auto message = createMessageWithStrings(strings, stringsOffset);

    copy_num(int, opcode, message, 0);
    copy_num(int, laneIndex, message, sizeof(int));
    copy_num(double, lanePos, message, sizeof(int) * 2);
    copy_num(double, speed, message, sizeof(int) * 2 + sizeof(double));

    printf("\tStub %d->%d | Sending addVehicle\n", ownerId, id);
    socket.send(message, zmq::send_flags::none);

    printf("\tStub %d->%d | Receiving addVehicle reply\n", ownerId, id);
    // unused reply, required by zeroMQ
    zmq::message_t reply;
    auto response = socket.recv(reply);
}