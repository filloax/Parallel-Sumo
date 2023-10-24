/*
messagingShared.hpp

Assorted functions related to connectivity of the system as a whole.

Author: Filippo Lenzi
*/

#pragma once

#include <string>

#include "psumoTypes.hpp"
#include "utils.hpp"

namespace psumo {

std::string getSocketName(std::string directory, partId_t from, partId_t to, int numThreads);
std::string getSyncSocketId(std::string dataFolder, partId_t partId);

zmq::socket_t* makeSocket(zmq::context_t&context_, zmq::socket_type  type_);
inline void* castPollSocket(zmq::socket_t& socket) { return socket.operator void*(); }
zmq::message_t createMessageWithStrings(std::vector<std::string>& strings, int offset = 0, int spaceAfter = 0);
std::vector<std::string> readStringsFromMessage(zmq::message_t& message, int offset = 0);


// Some inline wrappers for zmq used in debug to check for socket connections
// may remove later

#ifndef NDEBUG
static int socketCounts = 0;
#endif

inline void connect(zmq::socket_t& socket, const std::string addr) {
    socket.connect(addr);
    #ifndef NDEBUG
        // printStackTrace();
        socketCounts++;
        printf("\t\tConnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
    #endif
}
inline void bind(zmq::socket_t& socket, const std::string addr) {
    socket.bind(addr);
    #ifndef NDEBUG
        // printStackTrace();
        socketCounts++;
        printf("\t\tConnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
    #endif
}
inline void close(zmq::socket_t& socket) {
    socket.close();
    #ifndef NDEBUG
        // printStackTrace();
        socketCounts--;
        printf("\t\tDisconnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
    #endif
}

}

