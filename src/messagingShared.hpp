/*
messagingShared.hpp

Assorted functions related to connectivity of the system as a whole.

Author: Filippo Lenzi
*/

#pragma once

#include <string>
#include "PartitionManager.hpp"

namespace psumo {

std::string getSocketName(std::string directory, partId_t from, partId_t to, int numThreads);
std::string getSyncSocketId(std::string dataFolder, partId_t partId);

}

