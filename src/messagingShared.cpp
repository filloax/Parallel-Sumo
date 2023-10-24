/*
messagingShared.hpp

Assorted functions related to connectivity of the system as a whole.

Author: Filippo Lenzi
*/

#include "messagingShared.hpp"

#include <sstream>
#include <string>

#include "utils.hpp"

using namespace std;

// #ifdef USING_WIN
  // #define Z_USES_TCP
// #endif

#define SYNC_SOCKETS_START 4500
#define PART_SOCKETS_START 5400

namespace psumo {


int cantorPairing(int a, int b, int n) {
  // Unique number from pairs of different ints
  return (a + b) * (a + b + 1) / 2 + b;
}

string getSocketName(std::string dataFolder, partId_t from, partId_t to, int numThreads) {
    stringstream out;
    #ifndef Z_USES_TCP
        out << "ipc://" << dataFolder << "/sockets/" << from << "-" << to;
    #else
        int port = PART_SOCKETS_START + cantorPairing(from, to, numThreads);
        out << "tcp://127.0.0.1:" <<  port;
    #endif

    return out.str();
}

string getSyncSocketId(std::string dataFolder, partId_t partId) {
  std::stringstream out;
    #ifndef Z_USES_TCP
    out << "ipc://" << dataFolder << "/sockets/" << partId << "-main-s";
  #else
    int port = SYNC_SOCKETS_START + partId;
    out << "tcp://127.0.0.1:" <<  port;
  #endif

  return out.str();
}


zmq::socket_t* makeSocket(zmq::context_t& context_, zmq::socket_type type_)  {
    auto socket = new zmq::socket_t{context_, type_};
    socket->set(zmq::sockopt::linger, 0 );
    return socket;
}

zmq::message_t createMessageWithStrings(vector<string> &strings, int offset, int spaceAfter) {
    int totalSize = 0;
    for (auto str: strings) totalSize += str.size();

    // + vector.size(): account for null characters at end of each string
    zmq::message_t message(offset + spaceAfter + sizeof(int) + totalSize + strings.size());
    char* data = static_cast<char*>(message.data());

    // Also write an int with the vector size
    int vectorSize = strings.size();
    std::memcpy(data + offset, &vectorSize, sizeof(int));

    int writtenBytes = sizeof(int);
    for (int i = 0; i < strings.size(); i++) {
        std::memcpy(
            data + offset + writtenBytes, 
            (strings[i] + '\0').data(), strings[i].size() + 1
        );
        writtenBytes += strings[i].size() + 1;
    }

    return message;
}

std::vector<std::string> readStringsFromMessage(zmq::message_t &message, int offset) {
    const char* data = static_cast<const char*>(message.data());
    size_t size = message.size();

    int vectorSize;
    std::memcpy(&vectorSize, data + offset, sizeof(int));

    std::vector<std::string> result(vectorSize);
    int currentString = 0;

    size_t start = offset + sizeof(int);
    for (size_t i = start; i < size && currentString < vectorSize; ++i) {
        if (data[i] == '\0') {
            result[currentString] = string(&data[start], &data[i]);
            start = i + 1;
            currentString++;
        }
    }

    return result;
}


}