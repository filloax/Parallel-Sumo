/**
PartitionManager.cpp

Manages partition's internal SUMO simulation and sychronizes
with other partitions running in parallel.

Author: Phillip Taylor
*/
#include "PartitionManager.hpp"

#include <iostream>
#include <algorithm>
#include <iterator>
#include <libsumo/Edge.h>
#include <libsumo/Simulation.h>
#include <libsumo/Vehicle.h>
#include <queue>
#include <string>
#include <thread>
#include <time.h>
#include <algorithm>
#include <vector>
#include <shared_mutex>
#include <unistd.h>

#include <libsumo/libsumo.h>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <zmq.hpp>

#include "NeighborPartitionHandler.hpp"
#include "PartitionEdgesStub.hpp"
#include "src/ParallelSim.hpp"
#include "utils.hpp"
#include "args.hpp"

static int numInstancesRunning = 0;

using namespace libsumo;
using namespace std;

/**
Uses LibSumo methods to handle the partition internally.
Note that LibSumo is static, so each PartitionManager must be on its own process.
*/
PartitionManager::PartitionManager(
  const string binary,
  partId_t id, string& cfg, int endTime,
  std::vector<partId_t> neighborPartitions,
  std::vector<string> sumoArgs, Args& args
  ) :
  binary(binary),
  id(id),
  cfg(cfg),
  endTime(endTime),
  neighborPartitions(neighborPartitions),
  sumoArgs(sumoArgs),
  args(args),
  dataFolder(args.dataDir),
  running(false)
  {
    zcontext = zmq::context_t{1};
    coordinatorSocket = zmq::socket_t{zcontext, zmq::socket_type::req};
    for (partId_t partId : neighborPartitions) {
      auto stub = new PartitionEdgesStub(id, partId, args);
      neighborPartitionStubs[partId] = stub;
      auto clientHandler = new NeighborPartitionHandler(*this, partId, zcontext);
      neighborClientHandlers[partId] = clientHandler;
    }
  }

PartitionManager::~PartitionManager() {
  for (partId_t partId : neighborPartitions) {
    delete neighborPartitionStubs[partId];
    delete neighborClientHandlers[partId];
  }
}

void PartitionManager::setMyBorderEdges(std::vector<border_edge_t>& borderEdges) {
  for(border_edge_t e : borderEdges) {
    if(e.to == id)
      toBorderEdges.push_back(e);
    else if(e.from == id)
      fromBorderEdges.push_back(e);
  }
}

// Returns pid of simulation process
int PartitionManager::startPartition() {
  printf("Manager %d: creating process, cfg %s\n", id, cfg.c_str());
  running = true;
  int pid = fork();
  if (pid == 0) {
    runSimulation();
  }
  return pid;
}

std::vector<string> PartitionManager::getEdgeVehicles(const string& edgeId) {
  return Edge::getLastStepVehicleIDs(edgeId);
}
void PartitionManager::setVehicleSpeed(const string& vehId, double speed) {
  // Using slowDown instead of setspeed as original program did it
  Vehicle::slowDown(vehId, speed, Simulation::getDeltaT());
}
void PartitionManager::addVehicle(
  const string& vehId, const string& routeId, const string& vehType,
  const string& laneId, int laneIndex, double lanePos, double speed
) {
  string lanePosStr = std::to_string(lanePos);
  string speedStr = std::to_string(speed);
  Vehicle::add(
    vehId, routeId, vehType, "now", 
    laneId, lanePosStr, speedStr
  );
}


void PartitionManager::handleIncomingEdges(int num, std::vector<std::vector<string>>& prevVehicles) {
  for(int toEdgeIdx = 0; toEdgeIdx < num; toEdgeIdx++) {
    std::vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(toBorderEdges[toEdgeIdx].id);
    int fromId = toBorderEdges[toEdgeIdx].from;

    if(!edgeVehicles.empty()) {
      PartitionEdgesStub* partStub = neighborPartitionStubs[fromId];
      for(string veh : edgeVehicles) {
        auto it = std::find(prevVehicles[toEdgeIdx].begin(), prevVehicles[toEdgeIdx].end(), veh);
        // vehicle speed is to be updated in previous partition
        if(it != prevVehicles[toEdgeIdx].end()) {

          // check if vehicle has been transferred out of partition
          std::vector<string> trans = partStub->getEdgeVehicles(toBorderEdges[toEdgeIdx].id);
          if(std::find(trans.begin(), trans.end(), veh) != trans.end()) {
            // set from partition vehicle speed to next partition vehicle speed
            try {
              partStub->setVehicleSpeed(veh, Vehicle::getSpeed(veh));
            }
            catch(libsumo::TraCIException& e){
              stringstream err;
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

void PartitionManager::handleOutgoigEdges(int num, std::vector<std::vector<string>>& prevVehicles) {
  for(int fromEdgeIdx = 0; fromEdgeIdx < num; fromEdgeIdx++) {
    std::vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(toBorderEdges[fromEdgeIdx].id);
    int toId = fromBorderEdges[fromEdgeIdx].to;

    if(!edgeVehicles.empty()) {
      PartitionEdgesStub* partStub = neighborPartitionStubs[toId];
      for(string veh : edgeVehicles) {
        auto it = std::find(prevVehicles[fromEdgeIdx].begin(), prevVehicles[fromEdgeIdx].end(), veh);
        // vehicle is to be inserted in next partition
        if(it == prevVehicles[fromEdgeIdx].end()) {

          // check if vehicle not already on edge (if a vehicle starts on a border edge)
          std::vector<string> toVehs = partStub->getEdgeVehicles(fromBorderEdges[fromEdgeIdx].id);
          string route = Vehicle::getRouteID(veh);

          if(std::find(toVehs.begin(), toVehs.end(), veh) == toVehs.end()) {

            // check if vehicle is on split route
            /*
            int pos = veh.find("_part");
            if(pos != string::npos) {
              int routePos = route.find("_part");
              string routeSub = route.substr(0,routePos+5);
              // NOTE: Route is now always "_part0"
              // ERROR
              route = routeSub+"0";
              int routePart = 0;
              string firstEdge = router.getRouteEdges(route, toId)[0];
              while(firstEdge.compare(fromBorderEdges[fromEdgeIdx].id)) {
                routePart++;
                route = routeSub+std::to_string(routePart);
                firstEdge = router.getRouteEdges(route, toId)[0];
              }
            }
            */
            try {
              // add vehicle to next partition
              partStub->addVehicle(
                veh, route, Vehicle::getTypeID(veh),
                Vehicle::getLaneID(veh), 
                Vehicle::getLaneIndex(veh),
                Vehicle::getLanePosition(veh),
                Vehicle::getSpeed(veh)
              );
            }
            catch(std::exception& e){
              stringstream err;
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

void PartitionManager::arriveWaitBarrier() {
  int opcode = ParallelSim::SyncOps::BARRIER;
  zmq::message_t message;
  std::memcpy(message.data(), &opcode, sizeof(int));
  coordinatorSocket.send(message, zmq::send_flags::none);

  // Receive response, essentially blocking
  // output not needed so just pass the previous message
  auto _ = coordinatorSocket.recv(message);
}

void PartitionManager::signalFinish() {
  int opcode = ParallelSim::SyncOps::FINISHED;
  zmq::message_t message;
  std::memcpy(message.data(), &opcode, sizeof(int));
  coordinatorSocket.send(message, zmq::send_flags::none);

  // Receive response, essentially blocking
  // output not needed so just pass the previous message
  auto _ = coordinatorSocket.recv(message);
}

// Only run in new process
void PartitionManager::runSimulation() {
  pid_t pid;
  std::vector<string> simArgs {
    binary, 
    "-c", cfg, 
    "--start",
    "--netstate-dump", dataFolder+"/output"+std::to_string(id)+".xml"
  };
  simArgs.reserve(simArgs.size() + distance(sumoArgs.begin(), sumoArgs.end()));
  simArgs.insert(simArgs.end(),sumoArgs.begin(),sumoArgs.end());

  numInstancesRunning++;
  if (numInstancesRunning > 0) {
    stringstream msg;
    msg << "[WARN] More than one instance of PartitionManager running in this process, "
      << "remember that only one simulation can be run with LibSumo per process."
      << std::endl;
    std::cerr << msg.str();
  }

  // Start simulation in this process
  // Note: doesn't support GUI
  Simulation::start(simArgs);
  for (auto partId : neighborPartitions) {
    neighborClientHandlers[partId]->start();
  }

  // Wait for coordinator process to bind socket
  sleep(1);
  coordinatorSocket.connect(ParallelSim::getSyncSocketId(id, dataFolder));

  // ensure all servers have started before simulation begins
  arriveWaitBarrier();

  for (auto stub : neighborPartitionStubs) {
    stub.second->connect();
  }

  stringstream msg;
  msg << "partition " << id << " started in thread " << pthread_self() << " (port " << simArgs[4] << ")" << std::endl << std::endl;
  std::cout << msg.str();

  int numFromEdges = fromBorderEdges.size();
  int numToEdges = toBorderEdges.size();
  std::vector<std::vector<string>> prevToVehicles(numToEdges);
  std::vector<std::vector<string>> prevFromVehicles(numFromEdges);

  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->listenOn();
  }

  while(running && Simulation::getTime() < endTime) {
    Simulation::step();
    handleIncomingEdges(numToEdges, prevToVehicles);
    handleOutgoigEdges(numFromEdges, prevFromVehicles);
    // make sure every time step across partitions is synchronized
    arriveWaitBarrier();

    // Neighbor handler buffers add vehicle and set speed operations while the 
    // edge handling is going on in each barrier, apply them after to avoid
    // interference and then start again
    for (partId_t partId : neighborPartitions) {
      neighborClientHandlers[partId]->applyMutableOperations();
    }
  }

  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->stop();
    neighborPartitionStubs[partId]->disconnect();
  }

  stringstream msg2;
  msg2 << "partition " << id << " ended in thread " << pthread_self() << std::endl;
  std::cout << msg2.str();

  signalFinish();
  
  Simulation::close("ParallelSim terminated.");
  numInstancesRunning--;
}