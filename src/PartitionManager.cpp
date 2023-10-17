/**
PartitionManager.cpp

Manages partition's internal SUMO simulation and sychronizes
with other partitions running in parallel.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/
#include "PartitionManager.hpp"

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <libsumo/Edge.h>
#include <libsumo/Simulation.h>
#include <libsumo/Vehicle.h>
#include <queue>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <time.h>
#include <algorithm>
#include <vector>
#include <shared_mutex>

#include <libsumo/libsumo.h>
#include <zmq.hpp>

#include "messagingShared.hpp"
#include "NeighborPartitionHandler.hpp"
#include "PartitionEdgesStub.hpp"
#include "ParallelSim.hpp"
#include "partArgs.hpp"
#include "utils.hpp"
#include "args.hpp"

static int numInstancesRunning = 0;

using namespace libsumo;
using namespace std;
using namespace psumo;

/**
Uses LibSumo methods to handle the partition internally.
Note that LibSumo is static, so each PartitionManager must be on its own process.
*/
PartitionManager::PartitionManager(
  const string binary,
  partId_t id, string& cfg, int endTime,
  vector<partId_t> neighborPartitions,
  zmq::context_t& zcontext, int numThreads,
  vector<string> sumoArgs,
  #ifdef PSUMO_SINGLE_EXECUTABLE
  Args& args
  #else
  PartArgs& args
  #endif
  ) :
  binary(binary),
  id(id),
  cfg(cfg),
  endTime(endTime),
  neighborPartitions(neighborPartitions),
  zcontext(zcontext),
  sumoArgs(sumoArgs),
  args(args),
  numThreads(numThreads),
  running(false)
  {
    coordinatorSocket = zmq::socket_t{zcontext, zmq::socket_type::req};
    coordinatorSocket.set(zmq::sockopt::linger, 0 );
    for (partId_t partId : neighborPartitions) {
      auto stub = new PartitionEdgesStub(id, partId, numThreads, zcontext, args);
      neighborPartitionStubs[partId] = stub;
      auto clientHandler = new NeighborPartitionHandler(*this, partId);
      neighborClientHandlers[partId] = clientHandler;
    }
  }

PartitionManager::~PartitionManager() {
  #ifndef NDEBUG
    printf("=== Destroying partition manager %d... ===\n", id);
    printStackTrace();
  #endif
  for (partId_t partId : neighborPartitions) {
    delete neighborPartitionStubs[partId];
    delete neighborClientHandlers[partId];
  }
}

void PartitionManager::setMyBorderEdges(vector<border_edge_t>& borderEdges) {
  for(border_edge_t e : borderEdges) {
    if(e.to == id)
      incomingBorderEdges.push_back(e);
    else if(e.from == id)
      outgoingBorderEdges.push_back(e);
  }
}

// Returns pid of simulation process
int PartitionManager::startPartitionNewProcess() {
  cerr << "startPartitionNewProcess to be redone for platform-neutral stuff, currently unused" << endl;
  exit(EXIT_FAILURE);
  // printf("Manager %d: creating process, cfg %s\n", id, cfg.c_str());
  // running = true;
  // int pid = fork();
  // if (pid == -1) {
  //   cerr << "Error in forking to create partition process!" << endl;
  //   exit(-1);
  // }
  // if (pid == 0) {
  //   exit(-99);
  //   runSimulation();
  // }
  // return pid;
}

void PartitionManager::startPartitionLocalProcess() {
  printf("Manager %d: starting simulation, cfg %s\n", id, cfg.c_str());
  running = true;
  runSimulation();
}

vector<string> PartitionManager::getEdgeVehicles(const string& edgeId) {
  // For some reason, despite the signature, passing the C++ string
  // didn't work (as in, it didn't find the data)
  #ifndef NDEBUG
  try {
    logminor("Running getLastStepVehicleIDs({}[{}])\n", edgeId.c_str(), edgeId);
  #endif
  return Edge::getLastStepVehicleIDs(edgeId.c_str());
  #ifndef NDEBUG
  } catch(exception& e) {
    logerr("Error in getEdgeVehicles({}): {}", edgeId, e.what());
    exit(EXIT_FAILURE);
  }
  #endif
}

void PartitionManager::setVehicleSpeed(const string& vehId, double speed) {
  // Using slowDown instead of setspeed as original program did it
  // Also use .c_str() for same reason as [getEdgeVehicles]
  #ifndef NDEBUG
  try {
    logminor("Running setVehicleSpeed({}[{}], {})\n", vehId.c_str(), vehId, speed);
  #endif
  Vehicle::slowDown(vehId.c_str(), speed, Simulation::getDeltaT());
  #ifndef NDEBUG
  } catch(exception& e) {
    logerr("Error in setVehicleSpeed({}, {}): {}", vehId, speed, e.what());
    exit(EXIT_FAILURE);
  }
  #endif
}
void PartitionManager::addVehicle(
  const string& vehId, const string& routeId, const string& vehType,
  const string& laneId, int laneIndex, double lanePos, double speed
) {
  string lanePosStr = std::to_string(lanePos);
  string speedStr = std::to_string(speed);
  // Use .c_str() for same reason as [getEdgeVehicles]
  #ifndef NDEBUG
  try {
  #endif
  Vehicle::add(
    vehId.c_str(), routeId.c_str(), vehType.c_str(), "now", 
    laneId.c_str(), lanePosStr, speedStr
  );
  #ifndef NDEBUG
  } catch(exception& e) {
    logerr("Error in addVehicle({}, {}, {}, {}, {}, {}, {}): {}", 
      vehId, routeId, vehType, laneId, laneIndex, lanePos, speed, e.what());
    exit(EXIT_FAILURE);
  }
  #endif

}


void PartitionManager::handleIncomingEdges(int num, vector<vector<string>>& prevVehicles) {
  for(int toEdgeIdx = 0; toEdgeIdx < num; toEdgeIdx++) {
    vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(incomingBorderEdges[toEdgeIdx].id.c_str());
    int fromId = incomingBorderEdges[toEdgeIdx].from;

    if(!edgeVehicles.empty()) {
      PartitionEdgesStub* partStub = neighborPartitionStubs[fromId];
      for(string veh : edgeVehicles) {
        auto it = std::find(prevVehicles[toEdgeIdx].begin(), prevVehicles[toEdgeIdx].end(), veh);
        // vehicle speed is to be updated in previous partition
        if(it != prevVehicles[toEdgeIdx].end()) {

          // check if vehicle has been transferred out of partition
          vector<string> trans = partStub->getEdgeVehicles(incomingBorderEdges[toEdgeIdx].id);
          if(std::find(trans.begin(), trans.end(), veh) != trans.end()) {
            // set from partition vehicle speed to next partition vehicle speed
            try {
              partStub->setVehicleSpeed(veh, Vehicle::getSpeed(veh.c_str()));
            }
            catch(libsumo::TraCIException& e){
              stringstream err;
              err << "Part " << id << " | Exception in vehicle slowdown: " << e.what() << std::endl;
              cerr << err.str();
            }
          }
        }
      }
    }
    prevVehicles[toEdgeIdx] = edgeVehicles;
  }
}

void PartitionManager::handleOutgoigEdges(int num, vector<vector<string>>& prevVehicles) {
  for(int fromEdgeIdx = 0; fromEdgeIdx < num; fromEdgeIdx++) {
    vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(outgoingBorderEdges[fromEdgeIdx].id.c_str());
    int toId = outgoingBorderEdges[fromEdgeIdx].to;

    if(!edgeVehicles.empty()) {
      PartitionEdgesStub* partStub = neighborPartitionStubs[toId];
      for(string veh : edgeVehicles) {
        auto c_veh = veh.c_str();
        auto it = std::find(prevVehicles[fromEdgeIdx].begin(), prevVehicles[fromEdgeIdx].end(), veh);
        // vehicle is to be inserted in next partition
        if(it == prevVehicles[fromEdgeIdx].end()) {

          // check if vehicle not already on edge (if a vehicle starts on a border edge)
          vector<string> toVehs = partStub->getEdgeVehicles(outgoingBorderEdges[fromEdgeIdx].id);
          string route = Vehicle::getRouteID(c_veh);

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
              while(firstEdge.compare(outgoingBorderEdges[fromEdgeIdx].id)) {
                routePart++;
                route = routeSub+std::to_string(routePart);
                firstEdge = router.getRouteEdges(route, toId)[0];
              }
            }
            */
            try {
              // add vehicle to next partition
              partStub->addVehicle(
                veh, route, Vehicle::getTypeID(c_veh),
                Vehicle::getLaneID(c_veh), 
                Vehicle::getLaneIndex(c_veh),
                Vehicle::getLanePosition(c_veh),
                Vehicle::getSpeed(c_veh)
              );
            }
            catch(std::exception& e){
              stringstream err;
              err << "Part " << id << " | Exception in adding vehicle: " << e.what() << std::endl;
              cerr << err.str();
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
  zmq::message_t message(sizeof(int));
  std::memcpy(message.data(), &opcode, sizeof(int));
  coordinatorSocket.send(message, zmq::send_flags::none);

  printf("Manager %d | Waiting for barrier...\n", id); //TEMP

  // Receive response, essentially blocking
  // output not needed so just pass the previous message
  auto _ = coordinatorSocket.recv(message);

  printf("Manager %d | Reached barrier...\n", id); //TEMP
}

void PartitionManager::signalFinish() {
  int opcode = ParallelSim::SyncOps::FINISHED;
  zmq::message_t message(sizeof(int));
  std::memcpy(message.data(), &opcode, sizeof(int));
  coordinatorSocket.send(message, zmq::send_flags::none);

  // Receive response, essentially blocking
  // output not needed so just pass the previous message
  auto _ = coordinatorSocket.recv(message);
}

// Only run in new process
void PartitionManager::runSimulation() {
  printf("Manager %d | Starting simulation logic\n", id);

  pid_t pid;
  vector<string> simArgs {
    binary, 
    "-c", cfg, 
    // "--start",
    "--netstate-dump", args.dataDir+"/output"+std::to_string(id)+".xml"
  };
  simArgs.reserve(simArgs.size() + distance(sumoArgs.begin(), sumoArgs.end()));
  simArgs.insert(simArgs.end(),sumoArgs.begin(),sumoArgs.end());

  numInstancesRunning++;
  if (numInstancesRunning > 1) {
    stringstream msg;
    msg << "[WARN] [pid=" << getPid() << ",id=" << id << "] More than one instance of PartitionManager running in this process, "
      << "remember that only one simulation can be run with LibSumo per process."
      << std::endl;
    cerr << msg.str();
  }

  // Start simulation in this process
  // Note: doesn't support GUI
  stringstream startMsg;
  startMsg << "Manager " << id << " | Starting simulation with args: ";
  printVector(simArgs, "", " ", true, startMsg);
  cout << startMsg.str();

  bool success = false;
  pair<int, string> version;

  try {
    version = Simulation::start(simArgs);
    success = Simulation::isLoaded();
  } catch (exception& e) {}

  if (success) {
    printf("Manager %d | Simulation loaded with %d starting vehicles, ver. %d-%s\n", id, 
      Simulation::getLoadedNumber(), version.first, version.second.c_str());
  } else {
    stringstream msg;
    msg << "[ERR] [pid=" << getPid() << ",id=" << id << "] Simulation failed to load! Quitting" << std::endl;
    cerr << msg.str();
    exit(EXIT_FAILURE);
  }

  try {
    for (auto partId : neighborPartitions) {
      neighborClientHandlers[partId]->start();
    }
  } catch(zmq::error_t& e) {
    stringstream ss;
    ss << "Manager " << id << " | ZMQ Error in starting neighbor client handlers: " << e.what() << endl;
    cerr << ss.str();
    exit(-2);
  }

  // Wait for coordinator process to bind socket
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  try {
    coordinatorSocket.connect(psumo::getSyncSocketId(args.dataDir, id));
  } catch(zmq::error_t& e) {
    stringstream ss;
    ss << "Manager " << id << " | ZMQ Error in connecting to coordinator process: " << e.what() << endl;
    cerr << ss.str();
    exit(-3);
  }

  // ensure all servers have started before simulation begins
  arriveWaitBarrier();

  try {
    for (auto stub : neighborPartitionStubs) {
      stub.second->connect();
    }
  } catch(zmq::error_t& e) {
    stringstream ss;
    ss << "Manager " << id << " | ZMQ Error in connecting partition stub: " << e.what() << endl;
    cerr << ss.str();
    exit(-4);
  }

  stringstream msg;
  msg << "-- partition " << id << " started in process " << getPid() << "--" << std::endl;
  cout << msg.str();

  int numFromEdges = outgoingBorderEdges.size();
  int numToEdges = incomingBorderEdges.size();
  vector<vector<string>> prevToVehicles(numToEdges);
  vector<vector<string>> prevFromVehicles(numFromEdges);

  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->listenOn();
  }

  while(running && Simulation::getTime() < endTime) {
    Simulation::step();

    printf("Manager %d | Step done (%d/%d)\n", id, (int) Simulation::getTime(), endTime);
    handleIncomingEdges(numToEdges, prevToVehicles);
    printf("Manager %d | Handled incoming edges\n", id);
    handleOutgoigEdges(numFromEdges, prevFromVehicles);
    printf("Manager %d | Handled outgoing edges\n", id);
    // make sure every time step across partitions is synchronized
    arriveWaitBarrier();

    // Neighbor handler buffers add vehicle and set speed operations while the 
    // edge handling is going on in each barrier, apply them after to avoid
    // interference and then start again
    for (partId_t partId : neighborPartitions) {
      neighborClientHandlers[partId]->applyMutableOperations();
    }
  }

  logminor("Simulation done, barrier then closing connections...\n");
  arriveWaitBarrier();

  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->stop();
    neighborPartitionStubs[partId]->disconnect();
  }
  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->join();
  }

  log("FINISHED!\n");

  signalFinish();
  
  Simulation::close("ParallelSim terminated.");
  numInstancesRunning--;
}

template<typename... _Args > 
inline void PartitionManager::log(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "Manager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cout << msg.str();
}

template<typename... _Args > 
inline void PartitionManager::logminor(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "\tManager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cout << msg.str();
}

template<typename... _Args>
inline void PartitionManager::logerr(std::format_string<_Args...> format, _Args&&... args) {
    std::stringstream msg;
    msg << "Manager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args)...
    );
    std::cerr << msg.str();
}