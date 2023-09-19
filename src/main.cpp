/*
Main program for running a parallel SUMO simulation. startSim() runs simulation.
Can comment out getFilePaths() and partitionNetwork() after these functions
have already been executed to run startSim() with same partitions.

Author: Phillip Taylor
*/

#include <iostream>
#include <cstdlib>
#include <vector>
#include "ParallelSim.h"

int main(int argc, char* argv[]) {
    std::list<std::string> args(argv + 1, argv + argc);
    bool skipPart = false;
    for (auto itr = args.begin(); itr != args.end(); /*nothing*/)
    {
        if (itr->compare("--skip-part") == 0) {
            skipPart = true;
            itr = args.erase(itr);
            break;
        } else {
            ++itr;
        }
    }
    std::vector<std::string> argsVec(args.begin(), args.end());

    // params: host server, first port. sumo cfg file, gui option (true), number of threads
    ParallelSim client("localhost", 1337, "assets/simpleNet.sumocfg", true, 4, argsVec);
    client.getFilePaths();
    // param: true for metis partitioning, false for grid partitioning (only works for 2 partitions currently)
    if (!skipPart) {
        client.partitionNetwork(true);
    }
    client.startSim();
}
