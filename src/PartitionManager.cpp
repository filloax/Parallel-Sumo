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
#include <unordered_map>
#include <vector>
#include <shared_mutex>

#include <libsumo/libsumo.h>
#include <zmq.hpp>

#include "messagingShared.hpp"
#include "NeighborPartitionHandler.hpp"
#include "PartitionEdgesStub.hpp"
#include "ParallelSim.hpp"
#include "partArgs.hpp"
#include "src/psumoTypes.hpp"
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
  vector<partId_t>& neighborPartitions,
  unordered_map<partId_t, unordered_set<string>>& neighborRoutes,
  unordered_map<string, unordered_set<string>>& routeEndsInEdges,
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
  neighborRoutes(neighborRoutes),
  routeEndsInEdges(routeEndsInEdges),
  zcontext(zcontext),
  sumoArgs(sumoArgs),
  args(args),
  numThreads(numThreads),
  running(false)
  {
    coordinatorSocket = makeSocket(zcontext, zmq::socket_type::req);
    for (partId_t partId : neighborPartitions) {
      auto stub = new PartitionEdgesStub(id, partId, numThreads, zcontext, args);
      neighborPartitionStubs[partId] = stub;
      auto clientHandler = new NeighborPartitionHandler(*this, partId);
      neighborClientHandlers[partId] = clientHandler;
    }
  }

PartitionManager::~PartitionManager() {
  delete coordinatorSocket;
  for (partId_t partId : neighborPartitions) {
    delete neighborPartitionStubs[partId];
    delete neighborClientHandlers[partId];
  }
}

void PartitionManager::setBorderEdges(vector<border_edge_t>& borderEdges) {
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

#ifndef NDEBUG
  #define strarg_ string
#else
  #define strarg_ string&
#endif

vector<string> PartitionManager::getEdgeVehicles(const strarg_ edgeId) {
  // For some reason, despite the signature, passing the C++ string
  // didn't work (as in, it didn't find the data)
  #ifndef NDEBUG
  try {
    logminor("Running getLastStepVehicleIDs({})\n", edgeId.c_str(), edgeId);
  #endif
  return Edge::getLastStepVehicleIDs(edgeId.c_str());
  #ifndef NDEBUG
  } catch(exception& e) {
    logerr("Error in getEdgeVehicles({}): {}\n", edgeId, e.what());
    exit(EXIT_FAILURE);
  }
  #endif
}

bool PartitionManager::hasVehicle(const string vehId) {
  refreshVehicleIds();
  
  string vehIdToCheck(vehId);

  if (args.verbose) {
    stringstream msg;
    format_to(ostream_iterator<char>(msg), "Manager {} | Has vehicle: current vehicles are [", id);
    int i = 0;
    for (auto id : allVehicleIds) {
      msg << id;
      if (i < allVehicleIds.size() - 1) msg << ", ";
      i++;
    }
    msg << "]" << endl;
    cout << msg.str();
  }

  return allVehicleIds.contains(vehIdToCheck);
}

bool PartitionManager::hasVehicleInEdge(const strarg_ vehId, const strarg_ edgeId) {
  auto edgeVehicles = getEdgeVehicles(edgeId);
  auto idx = std::find(edgeVehicles.begin(), edgeVehicles.end(), vehId);
  return idx != edgeVehicles.end();
}

void PartitionManager::setVehicleSpeed(const strarg_ vehId, double speed) {
  // Using slowDown instead of setspeed as original program did it
  // Also use .c_str() for same reason as [getEdgeVehicles]
  #ifndef NDEBUG
  try {
    logminor("Running setVehicleSpeed({}, {})\n", vehId.c_str(), vehId, speed);
  #endif
  Vehicle::slowDown(vehId.c_str(), speed, Simulation::getDeltaT());
  #ifndef NDEBUG
  } catch(exception& e) {
    logerr("Error in setVehicleSpeed({}, {}): {}\n", vehId, speed, e.what());
    exit(EXIT_FAILURE);
  }
  #endif
}

void PartitionManager::addVehicle(
  const strarg_ vehId, const strarg_ routeId, const strarg_ vehType,
  const strarg_ laneId, int laneIndex, double lanePos, double speed
) {
  string lanePosStr = std::to_string(lanePos);
  string speedStr = std::to_string(speed);
  #ifndef NDEBUG
  try {
  #endif
  Vehicle::add(
  // Use .c_str() for same reason as [getEdgeVehicles]
    vehId.c_str(), routeId.c_str(), vehType.c_str(), "now", 
    "first", "base", speedStr
  );
  Vehicle::moveTo(vehId.c_str(), laneId.c_str(), lanePos);
  if (allVehicleIdsUpdated) {
    allVehicleIds.insert(vehId);
  }
  #ifndef NDEBUG
  logminor("Added vehicle {} to lane {}\n", vehId, laneId);
  } catch(exception& e) {
    logerr("Error in addVehicle({}, {}, {}, {}, {}, {}, {}): {}\n", 
      vehId, routeId, vehType, laneId, laneIndex, lanePos, speed, e.what());
    exit(EXIT_FAILURE);
  }
  #endif
}


void PartitionManager::handleIncomingEdges(int num, vector<vector<string>>& prevIncomingVehicles) {
  /*
  Temporarily test how the system works without this part
  (slowing down shadow vehicle on previous partition)
  ---
  for(int toEdgeIdx = 0; toEdgeIdx < num; toEdgeIdx++) {
    vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(incomingBorderEdges[toEdgeIdx].id.c_str());
    int fromId = incomingBorderEdges[toEdgeIdx].from;

    if(!edgeVehicles.empty()) {
      PartitionEdgesStub* partStub = neighborPartitionStubs[fromId];
      for(string veh : edgeVehicles) {
        auto it = std::find(prevIncomingVehicles[toEdgeIdx].begin(), prevIncomingVehicles[toEdgeIdx].end(), veh);
        // vehicle speed is to be updated in previous partition
        if(it != prevIncomingVehicles[toEdgeIdx].end()) {
          // check if vehicle has been transferred out of partition
          // check if vehicle is in that edge specifically, 
          // unlike the add check after, to avoid messing with its speed if not needed
          bool inEdge = partStub->hasVehicleInEdge(veh, incomingBorderEdges[toEdgeIdx].id);
          if(inEdge) {
            // set from partition vehicle speed to next partition vehicle speed
            #ifndef PSUMO_NO_EXC_CATCH
            try {
            #endif
              partStub->setVehicleSpeed(veh, Vehicle::getSpeed(veh.c_str()));
            #ifndef PSUMO_NO_EXC_CATCH
            }
            catch(exception& e){
              logerr("Part {} | Exception in setting speed of vehicle: {}\n", id, e.what());
            }
            #endif
          }
        }
      }
    }
    prevIncomingVehicles[toEdgeIdx] = edgeVehicles;
  }
  */
}

void PartitionManager::handleOutgoingEdges(int num, vector<vector<string>>& prevOutgoingVehicles) {
  for(int outEdgeIdx = 0; outEdgeIdx < num; outEdgeIdx++) {
    auto borderEdge = outgoingBorderEdges[outEdgeIdx];
    vector<string> edgeVehicles = Edge::getLastStepVehicleIDs(borderEdge.id.c_str());
    partId_t toId = borderEdge.to;

    if(!edgeVehicles.empty()) {
      auto toRoutesIt = neighborRoutes.find(toId);
      if (toRoutesIt == neighborRoutes.end()) {
        // No routes in neighbor somehow, continue
        continue;
      }
      unordered_set<string> toRoutes = toRoutesIt->second;

      auto routesEndingInEdgeIt = routeEndsInEdges.find(borderEdge.id);
      if (routesEndingInEdgeIt == routeEndsInEdges.end()) {
        // No local routes ending in this edge (meaning no vehicles
        // will pass to the next from here), continue
        continue;
      }
      unordered_set<string> routesEndingInEdge = routesEndingInEdgeIt->second;

      PartitionEdgesStub* partStub = neighborPartitionStubs[toId];
      
      for(string veh : edgeVehicles) {
        auto c_veh = veh.c_str();
        string route = Vehicle::getRouteID(c_veh);

        if (!toRoutes.contains(route)) {
          // Vehicle doesn't need to pass to neighbor
          continue;
        }

        if (!routesEndingInEdge.contains(route)) {
          // Vehicle passes to neighbor, but not from this edge
          // (Happens in some simulation edge cases)
          continue;
        }

        auto it = std::find(prevOutgoingVehicles[outEdgeIdx].begin(), prevOutgoingVehicles[outEdgeIdx].end(), veh);
        // vehicle is to be inserted in next partition
        if(it == prevOutgoingVehicles[outEdgeIdx].end()) {
          // check if vehicle not already on edge (if a vehicle starts on a border edge)
          // Used to check vehicles in the edge, change to this to save
          // message space, and to handle some edge cases a vehicle goes
          // from one border edge to another
          bool alreayInTarget = partStub->hasVehicle(veh);
          if(!alreayInTarget) {
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
            #ifndef PSUMO_NO_EXC_CATCH
            try {
            #endif
              // add vehicle to next partition
              partStub->addVehicle(
                veh, route, Vehicle::getTypeID(c_veh),
                Vehicle::getLaneID(c_veh), 
                Vehicle::getLaneIndex(c_veh),
                Vehicle::getLanePosition(c_veh),
                Vehicle::getSpeed(c_veh)
              );
            #ifndef PSUMO_NO_EXC_CATCH
            }
            catch(std::exception& e){
              stringstream err;
              err << "Part " << id << " | Exception in adding vehicle: " << e.what() << std::endl;
              cerr << err.str();
            }
            #endif
          }
        }
      }
    }
    prevOutgoingVehicles[outEdgeIdx] = edgeVehicles;
  }
}

void PartitionManager::arriveWaitBarrier() {
  int opcode = ParallelSim::SyncOps::BARRIER;
  zmq::message_t message(sizeof(int));
  std::memcpy(message.data(), &opcode, sizeof(int));
  coordinatorSocket->send(message, zmq::send_flags::none);

  logminor("Waiting for barrier...\n", id); //TEMP

  // Receive response, essentially blocking
  // output not needed so just pass the previous message
  auto _ = coordinatorSocket->recv(message);

  logminor("Reached barrier...\n", id); //TEMP
}

void PartitionManager::signalFinish() {
  int opcode = ParallelSim::SyncOps::FINISHED;
  zmq::message_t message(sizeof(int));
  std::memcpy(message.data(), &opcode, sizeof(int));
  logminor("Signaling partition end...\n");
  coordinatorSocket->send(message, zmq::send_flags::none);

  // Receive response, essentially blocking
  // output not needed so just pass blank one
  zmq::message_t __;
  auto _ = coordinatorSocket->recv(__);
  logminor("Signaled partition end...\n");
}

// Only run in new process
void PartitionManager::runSimulation() {
  logminor("Starting simulation logic\n", id);

  pid_t pid;
  vector<string> simArgs {
    binary, 
    "-c", cfg, 
    "--start",
    // Output vehicle paths
    "--netstate-dump", args.dataDir+"/output"+to_string(id)+".xml",
    // Log stdout/stderr
    "--log", args.dataDir+"/log"+to_string(id)+".txt"
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
    log("Simulation loaded with {} starting vehicles, ver. {} - {}\n", 
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
    logerr("ZMQ Error in starting neighbor client handlers: {}\n", e.what());
    exit(EXIT_FAILURE);
  }

  // Wait for coordinator process to bind socket
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  try {
    connect(*coordinatorSocket, psumo::getSyncSocketId(args.dataDir, id));
  } catch(zmq::error_t& e) {
    logerr("ZMQ Error in connecting to coordinator process: {}\n", e.what());
    exit(EXIT_FAILURE);
  }

  // ensure all servers have started before simulation begins
  arriveWaitBarrier();

  try {
    for (auto stub : neighborPartitionStubs) {
      stub.second->connect();
    }
  } catch(zmq::error_t& e) {
    logerr("ZMQ Error in connecting partition stub: {}\n", e.what());
    exit(EXIT_FAILURE);
  }

  stringstream msg;
  msg << "-- partition " << id << " started in process " << getPid() << "--" << std::endl;
  cout << msg.str();

  int numFromEdges = outgoingBorderEdges.size();
  int numToEdges = incomingBorderEdges.size();
  vector<vector<string>> prevIncomingVehicles(numToEdges);
  vector<vector<string>> prevOutgoingVehicles(numFromEdges);

  for (partId_t partId : neighborPartitions) {
    neighborClientHandlers[partId]->listenOn();
  }

  while(running && Simulation::getTime() < endTime) {
    Simulation::step();

    allVehicleIdsUpdated = false;

    logminor("Step done ({}/{})\n", (int) Simulation::getTime(), endTime);
    handleIncomingEdges(numToEdges, prevIncomingVehicles);
    logminor("Handled incoming edges\n");
    handleOutgoingEdges(numFromEdges, prevOutgoingVehicles);
    logminor("Handled outgoing edges\n");
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
  close(*coordinatorSocket);
 
  Simulation::close("ParallelSim terminated.");
  numInstancesRunning--;
}

void PartitionManager::refreshVehicleIds() {
  if (!allVehicleIdsUpdated) {
    lock_guard<mutex> lock(allVehicleIds_lock);

    vector<string> idVector = Vehicle::getIDList(); 
    allVehicleIds.clear();
    allVehicleIds.insert(idVector.begin(), idVector.end());
    allVehicleIdsUpdated = true;
  }
}

template<typename... _Args > 
inline void PartitionManager::log(std::format_string<_Args...> format, _Args&&... args_) {
    std::stringstream msg;
    msg << "Manager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args_)...
    );
    std::cout << msg.str();
}

template<typename... _Args > 
inline void PartitionManager::logminor(std::format_string<_Args...> format, _Args&&... args_) {
    if (!args.verbose) return;

    std::stringstream msg;
    msg << "\tManager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args_)...
    );
    std::cout << msg.str();
}

template<typename... _Args>
inline void PartitionManager::logerr(std::format_string<_Args...> format, _Args&&... args_) {
    std::stringstream msg;
    msg << "Manager " << id << " | ";
    std::format_to(
        std::ostreambuf_iterator<char>(msg), 
        std::forward<std::format_string<_Args...>>(format),
        std::forward<_Args>(args_)...
    );
    std::cerr << msg.str();
}