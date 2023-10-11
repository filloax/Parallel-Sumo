#pragma once

#include <string>
#include <vector>

#include "libs/traciapi/TraCIAPI.h"

typedef struct partitionPort {
    int partIdx;
    int port;
} partitionPort;

/**
 * Used by partition managers to get a connection for each partition's SUMO instance.
 * Connects to the partition's simulation plus its neighbors.
*/
class SumoConnectionRouter {

/* 
Possible improvements later: add checking to see if
the partition has "permissions" to do non-whitelisted
operations, only allowing some to be done in the not-owned
connection. Could be a performance issue, so avoid for now.

Every connection operation defaults to -1 as the partition index, 
*/

private:
    // Can be -1 for no owner, used for defaulting operations
    int ownerId;

    std::vector<int> handledPartitions;
    // idx = partition index
    std::vector<TraCIAPI> connections;
    // -1 if unhandled partition
    std::vector<int> partitionPorts;
    std::string host;

    void connectToPartition(int partId);
    void closePartition(int partId);
public:
    /**
     * @param host hostname
     * @param partitionPorts vector with the pair of partition/port of each neighbor or owner partition
     * @param ownerId optional, used to default the operation to the owner 
    */
    SumoConnectionRouter(std::string host, std::vector<partitionPort>& partitionPorts, int numParts, int ownerId = -1);

    void connectAll();
    void closeAll();
};