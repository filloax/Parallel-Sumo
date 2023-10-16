/*
messagingShared.hpp

Assorted functions related to connectivity of the system as a whole.

Author: Filippo Lenzi
*/

#include "messagingShared.hpp"

#include <sstream>
#include <string>

using namespace std;

// #ifdef USING_WIN
  // #define Z_USES_TCP
// #endif

#define SYNC_SOCKETS_START 4500
#define PART_SOCKETS_START 5400

int cantorPairing(int a, int b, int n) {
  // Unique number from pairs of different ints
  return (a + b) * (a + b + 1) / 2 + b;
}

string psumo::getSocketName(std::string dataFolder, partId_t from, partId_t to, int numThreads) {
    stringstream out;
    #ifndef Z_USES_TCP
        out << "ipc://" << dataFolder << "/sockets/" << from << "-" << to;
    #else
        int port = PART_SOCKETS_START + cantorPairing(from, to, numThreads);
        out << "tcp://127.0.0.1:" <<  port;
    #endif

    return out.str();
}


string psumo::getSyncSocketId(std::string dataFolder, partId_t partId) {
  std::stringstream out;
    #ifndef Z_USES_TCP
    out << "ipc://" << dataFolder << "/sockets/" << partId << "-main-s";
  #else
    int port = SYNC_SOCKETS_START + partId;
    out << "tcp://127.0.0.1:" <<  port;
  #endif

  return out.str();
}

