/**
PartitionManager.cpp

Manages partition's internal SUMO simulation and sychronizes
with other partitions running in parallel.

Author: Phillip Taylor
*/


#include <iostream>
#include <algorithm>
#include <iterator>
#include <string>
#include <time.h>
#include <algorithm>
#include <vector>
#include <shared_mutex>
#include <unistd.h>
#include "libs/traciapi/TraCIAPI.h"
#include "PartitionManager.hpp"
#include "utils.hpp"
#include "args.hpp"

PartitionManager::PartitionManager(const std::string binary, int id, std::barrier<>& syncBarrier, 
  SumoConnectionRouter& router, std::string& cfg, int port, int endTime,
  std::vector<std::string> sumoArgs, Args& args) :
  SUMO_BINARY(binary),
  id(id),
  syncBarrier(syncBarrier),
  router(router),
  cfg(cfg),
  port(port),
  endTime(endTime),
  sumoArgs(sumoArgs),
  args(args),
  dataFolder("data"),
  running(false)
  {
  }

void PartitionManager::setMyBorderEdges(std::vector<border_edge_t>& borderEdges) {
  for(border_edge_t e : borderEdges) {
    if(e.to == id)
      toBorderEdges.push_back(e);
    else if(e.from == id)
      fromBorderEdges.push_back(e);
  }
}

bool PartitionManager::startPartition() {
  printf("Manager %d: creating thread on port %d, cfg %s\n", id, port, cfg.c_str());
  running = true;
  thread = std::thread(&PartitionManager::internalSim, this);
  return true; // TODO: check if thread started with success
}

void PartitionManager::waitForPartition() {
  thread.join();
}

void PartitionManager::closePartition() {
  router.closeAll();
  running = false; // stops thread at loop start
  printf("Manager %d: closing... (not immediate, thread will stop as soon as possible)\n", id);
}

void PartitionManager::connect() {
  router.connectAll();
}

void PartitionManager::handleIncomingEdges(int num, std::vector<std::vector<std::string>>& prevVehicles) {
  for(int toEdgeIdx = 0; toEdgeIdx < num; toEdgeIdx++) {
    std::vector<std::string> currVehicles = router.getEdgeVehicles(toBorderEdges[toEdgeIdx].id);

    if(!currVehicles.empty()) {
      for(std::string veh : currVehicles) {
        auto it = std::find(prevVehicles[toEdgeIdx].begin(), prevVehicles[toEdgeIdx].end(), veh);
        // vehicle speed is to be updated in previous partition
        if(it != prevVehicles[toEdgeIdx].end()) {
          int fromId = toBorderEdges[toEdgeIdx].from;

          // check if vehicle has been transferred out of partition
          std::vector<std::string> trans = router.getEdgeVehicles(toBorderEdges[toEdgeIdx].id, fromId);
          if(std::find(trans.begin(), trans.end(), veh) != trans.end()) {
            // set from partition vehicle speed to next partition vehicle speed
            try {
              router.slowDown(veh, myConn.vehicle.getSpeed(veh), fromId);
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
    prevVehicles[toEdgeIdx] = currVehicles;
  }
}

void PartitionManager::handleOutgoigEdges(int num, std::vector<std::vector<std::string>>& prevVehicles) {
  for(int fromEdgeIdx = 0; fromEdgeIdx < num; fromEdgeIdx++) {
    std::vector<std::string> currVehicles = router.getEdgeVehicles(fromBorderEdges[fromEdgeIdx].id);

    if(!currVehicles.empty()) {
      for(std::string veh : currVehicles) {
        auto it = std::find(prevVehicles[fromEdgeIdx].begin(), prevVehicles[fromEdgeIdx].end(), veh);
        // vehicle is to be inserted in next partition
        if(it == prevVehicles[fromEdgeIdx].end()) {
          int toId = fromBorderEdges[fromEdgeIdx].to;

          // check if vehicle not already on edge (if a vehicle starts on a border edge)
          std::vector<std::string> toVehs = router.getEdgeVehicles(fromBorderEdges[fromEdgeIdx].id, toId);
          std::string route = myConn.vehicle.getRouteID(veh);

          if(std::find(toVehs.begin(), toVehs.end(), veh) == toVehs.end()) {

            // check if vehicle is on split route
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
            try {
              // add vehicle to next partition
              router.add(
                veh, route, myConn.vehicle.getTypeID(veh),
                std::to_string(myConn.vehicle.getLaneIndex(veh)), 
                std::to_string(myConn.vehicle.getLanePosition(veh)),
                std::to_string(myConn.vehicle.getSpeed(veh)),
                toId
              );
              // move vehicle to proper lane position in next partition
              router.moveTo(veh, myConn.vehicle.getLaneID(veh), myConn.vehicle.getLanePosition(veh), toId);
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
    prevVehicles[fromEdgeIdx] = currVehicles;
  }
}


void PartitionManager::internalSim() {
  pid_t pid;
  std::string portStr = std::to_string(port);
  std::vector<std::string> simArgs {
    SUMO_BINARY, 
    "-c", cfg, 
    "--remote-port", portStr, 
    "--start",
    "--netstate-dump", dataFolder+"/output"+std::to_string(id)+".xml"
  };
  simArgs.reserve(simArgs.size() + distance(sumoArgs.begin(), sumoArgs.end()));
  simArgs.insert(simArgs.end(),sumoArgs.begin(),sumoArgs.end());

  switch(pid = fork()){
    case -1:
      // fork() has failed
      perror("fork");
      break;
    case 0:
      // execute sumo simulation
      for (int i = 0; i < simArgs.size(); i++) printf("%s ", simArgs[i].c_str()); printf("\n");
      EXECVP_CPP(simArgs);
      std::cout << "execv() has failed" << std::endl;
      exit(EXIT_FAILURE);
      break;
  }

  // wait for server to startup (1 second)
  nanosleep((const struct timespec[]){{1, 0}}, NULL);
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

  while(running && myConn.simulation.getTime() < endTime) {
    myConn.simulationStep();
    handleIncomingEdges(numToEdges, prevToVehicles);
    handleOutgoigEdges(numFromEdges, prevFromVehicles);

    // make sure every time step across partitions is synchronized
    syncBarrier.arrive_and_wait();
  }
  std::stringstream msg2;
  msg2 << "partition " << id << " ended in thread " << pthread_self() << std::endl;
  std::cout << msg2.str();
  closePartition();
}
