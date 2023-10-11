#include "SumoConnectionRouter.hpp"
#include "utils.hpp"

SumoConnectionRouter::SumoConnectionRouter(std::string host, std::vector<partitionPort>& _partitionPorts, int numParts, int ownerId):
host(host),
ownerId(ownerId)
{
    connections.reserve(numParts);
    partitionPorts.reserve(numParts);
    for (auto partitionPort : _partitionPorts) {
        handledPartitions.push_back(partitionPort.partIdx);
        partitionPorts[partitionPort.partIdx] = partitionPort.port;
    }
    for (int i = 0; i < numParts; i++) {
        // partition not in handled ones
        if (std::find(handledPartitions.begin(), handledPartitions.end(), i) == handledPartitions.end()) {
            partitionPorts[i] = -1;
        }
    }
}

void SumoConnectionRouter::connectToPartition(int partId) {
    int port = partitionPorts[partId];
    if (port < 0) {
        throw std::invalid_argument("Partition " + std::to_string(ownerId) + " | Cannot connect to partition " + std::to_string(partId) + ", not a neighbor!");
    }
    auto connection = connections[partId];
    try {
        connection.connect(host, port);
    } catch(std::exception& e) {
        std::stringstream msg;
        msg << "Partition " << ownerId << " | Exception in connecting to TraCI API at part " << partId << ": " << e.what() << std::endl;
        msg << getStackTrace() << std::endl;
        std::cerr << msg.str();
        std::exit(-10);
    }
}

void SumoConnectionRouter::connectAll() {
    for (int partId : handledPartitions) {
        connectToPartition(partId);
    }
}

void SumoConnectionRouter::closePartition(int partId) {
    int port = partitionPorts[partId];
    if (port < 0) {
        throw std::invalid_argument("Partition " + std::to_string(ownerId) + " | Cannot close partition " + std::to_string(partId) + ", not a neighbor!");
    }
    auto connection = connections[partId];
    try {
        connection.close();
    } catch(std::exception& e) {
        std::stringstream msg;
        msg << "Partition " << ownerId << " | Exception in closing TraCI API to partition " << partId <<": " << e.what() << std::endl;
        msg << getStackTrace() << std::endl;
        std::cerr << msg.str();
    }
}

void SumoConnectionRouter::closeAll() {
    for (int partId : handledPartitions) {
        closePartition(partId);
    }
}