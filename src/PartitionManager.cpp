/**
PartitionManager.cpp

Manages partition's internal SUMO simulation and sychronizes
with other partitions running in parallel.

Author: Phillip Taylor
*/


#include <iostream>
#include <algorithm>
#include <iterator>
#include <libsumo/Edge.h>
#include <libsumo/Simulation.h>
#include <libsumo/Vehicle.h>
#include <string>
#include <time.h>
#include <algorithm>
#include <vector>
#include <shared_mutex>
#include <unistd.h>

#include <libsumo/libsumo.h>
#include <boost/interprocess/managed_shared_memory.hpp>

#include "PartitionManager.hpp"
#include "utils.hpp"
#include "args.hpp"

static int numInstancesRunning = 0;

using namespace libsumo;

/**
Uses LibSumo methods to handle the partition internally.
Note that LibSumo is static, so each PartitionManager must be on its own process.
*/
PartitionManager::PartitionManager(const std::string binary, partId_t id, 
  std::string& cfg, int port, int endTime,
  std::vector<std::string> sumoArgs, Args& args) :
  SUMO_BINARY(binary),
  id(id),
  cfg(cfg),
  port(port),
  endTime(endTime),
  sumoArgs(sumoArgs),
  args(args),
  dataFolder("data"),
  running(false),
  numPartitions(1)
  {

  }

PartitionManager::~PartitionManager() {
}

void PartitionManager::setMyBorderEdges(std::vector<border_edge_t>& borderEdges) {
  for(border_edge_t e : borderEdges) {
    if(e.to == id)
      toBorderEdges.push_back(e);
    else if(e.from == id)
      fromBorderEdges.push_back(e);
  }
}

void PartitionManager::setNumPartitions(int numParts) {
  numPartitions = numParts;
}

bool PartitionManager::startPartition() {
  printf("Manager %d: creating process, cfg %s\n", id, cfg.c_str());
  running = true;
  int pid = fork();
  if (pid == 0) {
    runSimulation();
  } else {
    return true;
  }
}

void PartitionManager::handleIncomingEdges(int num, std::vector<std::vector<std::string>>& prevVehicles) {
  for(int toEdgeIdx = 0; toEdgeIdx < num; toEdgeIdx++) {
    std::vector<std::string> edgeVehicles = Edge::getLastStepVehicleIDs(toBorderEdges[toEdgeIdx].id);

    if(!edgeVehicles.empty()) {
      for(std::string veh : edgeVehicles) {
        auto it = std::find(prevVehicles[toEdgeIdx].begin(), prevVehicles[toEdgeIdx].end(), veh);
        // vehicle speed is to be updated in previous partition
        if(it != prevVehicles[toEdgeIdx].end()) {
          int fromId = toBorderEdges[toEdgeIdx].from;

          // check if vehicle has been transferred out of partition
          std::vector<std::string> trans = router.getEdgeVehicles(toBorderEdges[toEdgeIdx].id, fromId);
          if(std::find(trans.begin(), trans.end(), veh) != trans.end()) {
            // set from partition vehicle speed to next partition vehicle speed
            try {
              router.slowDown(veh, Vehicle::getSpeed(veh), fromId);
            }
            catch(libsumo::TraCIException& e){
              std::stringstream err;
              err << "Part " << id << " | Exception in vehicle slowdown: " << e.what() << std::endl;
              std::cerr << err.str();
            }
          }
        }
      }
    }
    prevVehicles[toEdgeIdx] = edgeVehicles;
  }
}

void PartitionManager::handleOutgoigEdges(int num, std::vector<std::vector<std::string>>& prevVehicles) {
  for(int fromEdgeIdx = 0; fromEdgeIdx < num; fromEdgeIdx++) {
    std::vector<std::string> edgeVehicles = Edge::getLastStepVehicleIDs(toBorderEdges[fromEdgeIdx].id);

    if(!edgeVehicles.empty()) {
      for(std::string veh : edgeVehicles) {
        auto it = std::find(prevVehicles[fromEdgeIdx].begin(), prevVehicles[fromEdgeIdx].end(), veh);
        // vehicle is to be inserted in next partition
        if(it == prevVehicles[fromEdgeIdx].end()) {
          int toId = fromBorderEdges[fromEdgeIdx].to;

          // check if vehicle not already on edge (if a vehicle starts on a border edge)
          std::vector<std::string> toVehs = router.getEdgeVehicles(fromBorderEdges[fromEdgeIdx].id, toId);
          std::string route = Vehicle::getRouteID(veh);

          if(std::find(toVehs.begin(), toVehs.end(), veh) == toVehs.end()) {

            // check if vehicle is on split route
            /*
            int pos = veh.find("_part");
            if(pos != std::string::npos) {
              int routePos = route.find("_part");
              std::string routeSub = route.substr(0,routePos+5);
              // NOTE: Route is now always "_part0"
              // ERROR
              route = routeSub+"0";
              int routePart = 0;
              std::string firstEdge = router.getRouteEdges(route, toId)[0];
              while(firstEdge.compare(fromBorderEdges[fromEdgeIdx].id)) {
                routePart++;
                route = routeSub+std::to_string(routePart);
                firstEdge = router.getRouteEdges(route, toId)[0];
              }
            }
            */
            try {
              // add vehicle to next partition
              router.addVehicle(
                veh, route, router.getVehicleType(veh),
                std::to_string(router.getVehicleLaneIndex(veh)), 
                std::to_string(router.getVehicleLanePosition(veh)),
                std::to_string(router.getVehicleSpeed(veh)),
                toId
              );
              // move vehicle to proper lane position in next partition
              router.moveTo(veh, Vehicle::getLaneID(veh), Vehicle::getLanePosition(veh), toId);
            }
            catch(libsumo::TraCIException& e){
              std::stringstream err;
              err << "Part " << id << " | Exception in adding vehicle: " << e.what() << std::endl;
              std::cerr << err.str();
            }
          }
        }
      }
    }
    prevVehicles[fromEdgeIdx] = edgeVehicles;
  }
}

// Only run in new process
void PartitionManager::runSimulation() {
  pid_t pid;
  std::string portStr = std::to_string(port);
  std::string numPartitionsStr = std::to_string(numPartitions);
  std::vector<std::string> simArgs {
    SUMO_BINARY, 
    "-c", cfg, 
    "--remote-port", portStr, 
    "--num-clients", numPartitionsStr,
    "--start",
    "--netstate-dump", dataFolder+"/output"+std::to_string(id)+".xml"
  };
  simArgs.reserve(simArgs.size() + distance(sumoArgs.begin(), sumoArgs.end()));
  simArgs.insert(simArgs.end(),sumoArgs.begin(),sumoArgs.end());

  numInstancesRunning++;
  if (numInstancesRunning > 0) {
    std::stringstream msg;
    msg << "[WARN] More than one instance of PartitionManager running in this process, "
      << "remember that only one simulation can be run with LibSumo per process."
      << std::endl;
    std::cerr << msg.str();
  }

  // Start simulation in this process
  // Note: doesn't support GUI
  Simulation::start(simArgs);

  // ensure all servers have started before simulation begins
  syncBarrier.arrive_and_wait();
  connect();
  std::stringstream msg;
  msg << "partition " << id << " started in thread " << pthread_self() << " (port " << simArgs[4] << ")" << std::endl << std::endl;
  std::cout << msg.str();

  int numFromEdges = fromBorderEdges.size();
  int numToEdges = toBorderEdges.size();
  std::vector<std::vector<std::string>> prevToVehicles(numToEdges);
  std::vector<std::vector<std::string>> prevFromVehicles(numFromEdges);

  while(running && Simulation::getTime() < endTime) {
    Simulation::step();
    handleIncomingEdges(numToEdges, prevToVehicles);
    handleOutgoigEdges(numFromEdges, prevFromVehicles);

    // make sure every time step across partitions is synchronized
    syncBarrier.arrive_and_wait();
  }
  std::stringstream msg2;
  msg2 << "partition " << id << " ended in thread " << pthread_self() << std::endl;
  std::cout << msg2.str();
  
  Simulation::close("ParallelSim terminated.");
  numInstancesRunning--;
}
