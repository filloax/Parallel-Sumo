/**
PartitionManager.cpp

Manages partition's internal SUMO simulation and sychronizes
with other partitions running in parallel.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/
#include "PartitionManager.hpp"

#include <bits/chrono.h>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <libsumo/Edge.h>
#include <libsumo/Simulation.h>
#include <libsumo/Vehicle.h>
#include <mutex>
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

#include "libs/tinyxml2.h"
#include "messagingShared.hpp"
#include "NeighborPartitionHandler.hpp"
#include "PartitionEdgesStub.hpp"
#include "ParallelSim.hpp"
#include "partArgs.hpp"
#include "src/globals.hpp"
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
  float lastDepartTime,
  zmq::context_t& zcontext, int numThreads,
  vector<string> sumoArgs,
  PartArgs& args
  ) :
  binary(binary),
  id(id),
  cfg(cfg),
  endTime(endTime),
  neighborPartitions(neighborPartitions),
  neighborRoutes(neighborRoutes),
  routeEndsInEdges(routeEndsInEdges),
  lastDepartTime(lastDepartTime),
  zcontext(zcontext),
  sumoArgs(sumoArgs),
  args(args),
  numThreads(numThreads),
  running(false)
  {
    coordinatorSocket = makeSocket(zcontext, zmq::socket_type::req);
    for (partId_t partId : neighborPartitions) {
      auto stub = new PartitionEdgesStub(*this, partId, numThreads, zcontext, args);
      neighborPartitionStubs[partId] = stub;
      auto clientHandler = new NeighborPartitionHandler(*this, partId);
      neighborClientHandlers[partId] = clientHandler;
    }

    logminor("Initialized. lastDepartTime={}, cfg={}\n", lastDepartTime, cfg);
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

const char* getRoutesFilesValue(string cfg) {
  tinyxml2::XMLDocument cfgDoc;
  tinyxml2::XMLError e = cfgDoc.LoadFile(cfg.c_str());
  if(e) {
    cerr <<  cfgDoc.ErrorIDToName(e) << endl;
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* cfgEl = cfgDoc.FirstChildElement("configuration");
  if (cfgEl == nullptr) {
    perror("sumo config error: no configuration\n");
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* inputElement = cfgEl->FirstChildElement("input");
  if (!inputElement) {
    perror("sumo config error: no input element in configuration\n");
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* routeFilesElement = inputElement->FirstChildElement("route-files");
  if (!routeFilesElement) {
    perror("sumo config error: no route files element in configuration\n");
    exit(EXIT_FAILURE);
  }

  const char* routeFilesValue = routeFilesElement->Attribute("value");
  if (!routeFilesValue) {
    perror("sumo config error: no value attribute in route files\n");
    exit(EXIT_FAILURE);
  }
  return routeFilesValue;
}

void PartitionManager::loadRouteMetadata() {
  auto routeFilesValue = getRoutesFilesValue(cfg);

  // NOTE: this assumes that there is only one route file, which is the output for 
  // the partitioning script
  const filesystem::path dir = filesystem::path(cfg).relative_path().parent_path();
  const filesystem::path routeFile = dir / routeFilesValue;
  tinyxml2::XMLDocument routeDoc;
  tinyxml2::XMLError e = routeDoc.LoadFile(routeFile.c_str());
  if(e) {
    logerr("{} - when loading {}\n", routeDoc.ErrorIDToName(e), routeFile.string());
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* routesEl = routeDoc.FirstChildElement("routes");
  if (routesEl == nullptr) {
    std::cout << "sumo routes file error: no routes" << std::endl;
    exit(EXIT_FAILURE);
  }

  for (
    tinyxml2::XMLElement* route = routesEl->FirstChildElement("route"); 
    route; 
    route = route->NextSiblingElement("route")
  ) {
    auto routeId = route->Attribute("id");
    if (routeId) {
      string routeIdStr(routeId);
      int partIndex = routeIdStr.find("_part");
      // Route id contains part -> is multipart
      if (partIndex != string::npos) {
        multipartRoutes.insert(routeIdStr.substr(0, partIndex));
      }
    } else {
      logerr("sumo routes file error: route with no id!\n");
      exit(EXIT_FAILURE);
    }
  }
}

void PartitionManager::enableTimeMeasures() {
  measureSimTime = true;
  measureInteractTime = true;
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
    logminor("Running getLastStepVehicleIDs({})\n", edgeId, edgeId);
  #endif

  return Edge::getLastStepVehicleIDs(edgeId);
  
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
    format_to(ostream_iterator<char>(msg), "\tManager {} | hasVehicle: current vehicles are [", id);
    int i = 0;
    for (auto id : allVehicleIds) {
      msg << id;
      if (i < allVehicleIds.size() - 1) msg << ", ";
      i++;
    }
    msg << "]" << endl;
    cout << msg.str();
  }

  // logminor("PRE CONTAINS\n");
  // TODO: it seems that this sometimes leads to a segfault when multiple neighbor handler threads
  // access it, but it should be thread-safe; investigate later
  auto found = std::find(allVehicleIds.begin(), allVehicleIds.end(), vehIdToCheck);
  // logminor("POST CONTAINS\n");
  return found != allVehicleIds.end();
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
    logminor("Running setVehicleSpeed({}, {})\n", vehId, vehId, speed);
  #endif

  Vehicle::slowDown(vehId, speed, Simulation::getDeltaT());

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
  string routeIdAdapted;
  // Adapt vehicle routes in case of multipart routes
  if (multipartRoutes.contains(routeId)) {
    if (vehicleMultipartRouteProgress.contains(vehId)) {
      // Will be set again when the vehicle exits, no need to set it here
      int newPartProgress = vehicleMultipartRouteProgress[vehId] + 1;
      routeIdAdapted = routeId + "_part" + to_string(newPartProgress); 
    } else {
      // Initialize it here, came from other partition
      vehicleMultipartRouteProgress[vehId] = 0;
      routeIdAdapted = routeId + "_part0"; 
    }

    auto routes = Route::getIDList();
    auto routeIt = find(routes.begin(), routes.end(), routeIdAdapted);

    // Edge case: if this part's route doesn't exist, it means a vehicle was added again after 
    // it did all the route parts, meaning its total route ends on another partition and on a
    // border edge TO this partition
    if (routeIt == routes.end()) {
      return;
    }
  } else {
    routeIdAdapted = routeId;
  }

  string lanePosStr = std::to_string(lanePos);
  string speedStr = std::to_string(speed);

  #ifndef NDEBUG
  try {
  #endif

    Vehicle::add(
      vehId, routeIdAdapted, vehType, "now", 
      "first", "base", speedStr
    );

    #ifndef NTRYCHECKS
    try {
    #endif

      Vehicle::moveTo(vehId, laneId, lanePos);
      if (allVehicleIdsUpdated) {
        allVehicleIds.insert(vehId);
      }

    #ifndef NTRYCHECKS
    } catch(exception& e) {
      logerr("[WARN] Error in addVehicle, moveTo({}, {}, {}): {} (still continuing)\n", 
        vehId, laneId, lanePos, e.what());
    }
    #endif

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

        // check if vehicle is on split route
        int routePartIdx = route.find("_part");
        bool isMultipart = routePartIdx != string::npos;
        if (isMultipart) {
          // Pass just the "main" route id to addvehicle
          string numStr = route.substr(routePartIdx + 5, route.size() - routePartIdx - 5);
          int routePartNum = std::stoi(numStr);
          vehicleMultipartRouteProgress[veh] = routePartNum;
          route = route.substr(0, routePartIdx);
        }

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

void PartitionManager::finishStepWait() {
  int opcode = ParallelSim::SyncOps::BARRIER_STEP;
  bool maybeFinished = isMaybeFinished();

  zmq::message_t message(sizeof(int) + sizeof(bool));
  auto data = static_cast<char*>(message.data());
  std::memcpy(data, &opcode, sizeof(int));
  std::memcpy(data + sizeof(int), &maybeFinished, sizeof(bool));

  coordinatorSocket->send(message, zmq::send_flags::none);

  logminor("Waiting for step end barrier, maybe finished: {}...\n", maybeFinished);

  // Receive response, essentially blocking
  zmq::message_t reply(sizeof(bool));
  auto result = coordinatorSocket->recv(reply);
  std::memcpy(&finished, reply.data(), sizeof(bool));

  logminor("Reached step end barrier, is finished: {}...\n", finished);
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

void PartitionManager::incMsgCount(bool outgoing) {
  if (args.logMsgNum) {
    if (outgoing) {
      lock_guard<mutex> lock(msgCountLockOut);
      msgCountOut++;
    } else {
      lock_guard<mutex> lock(msgCountLockIn);
      msgCountIn++;
    }
  }
}

bool PartitionManager::isMaybeFinished() {
  return Simulation::getTime() > lastDepartTime + 1 && Vehicle::getIDCount() == 0;
}

bool isFinished(float simTime, int endTime, bool finished) {
  if (endTime > -1) {
    return simTime >= endTime;
  } else {
    return finished;
  }
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
    "--netstate-dump", OUTDIR+"/output"+to_string(id)+".xml",
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
  // Note: doesn't support GUI unless the define for libsumo GUI is enabled
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
      Vehicle::getIDCount(), version.first, version.second.c_str());
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

  filesystem::path logVehiclesFile, logMsgsFile;
  if (args.logHandledVehicles) {
    logVehiclesFile = filesystem::path(args.dataDir) / ("stepVehicles" + to_string(id) + ".csv");
    std::ofstream(logVehiclesFile, std::ios::out) << "time,vehNo\n";
  }
  if (args.logMsgNum) {
    logMsgsFile = filesystem::path(args.dataDir) / ("msgNum" + to_string(id) + ".csv");
    std::ofstream(logMsgsFile, std::ios::out) << "time,msgs_in,msgs_out\n";
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

  chrono::steady_clock::duration simTime, commTime;//, handleTime;
  simTime = chrono::steady_clock::duration::zero();
  commTime = chrono::steady_clock::duration::zero();
  // handleTime = chrono::steady_clock::duration::zero();
  chrono::steady_clock::time_point timeBefore;

  while(running && !isFinished(Simulation::getTime(), endTime, finished)) {
    if (measureSimTime) timeBefore = chrono::steady_clock::now();
    Simulation::step();
    if (measureSimTime) simTime += chrono::steady_clock::now() - timeBefore;

    allVehicleIdsUpdated = false;

    if (endTime >= 0)
      logminor("Step done ({}/{})\n", (int) Simulation::getTime(), endTime);
    else
      logminor("Step done ({})\n", (int) Simulation::getTime());

    if (args.logHandledVehicles) {
      ofstream(logVehiclesFile, ios::app) << Simulation::getTime() << "," << Vehicle::getIDCount() << "\n";
    }

    if (measureInteractTime) timeBefore = chrono::steady_clock::now();

    handleIncomingEdges(numToEdges, prevIncomingVehicles);
    logminor("Handled incoming edges\n");
    handleOutgoingEdges(numFromEdges, prevOutgoingVehicles);
    logminor("Handled outgoing edges\n");

    if (measureInteractTime) commTime += chrono::steady_clock::now() - timeBefore;

    // make sure every time step across partitions is synchronized
    finishStepWait();

    // if (measureInteractTime) timeBefore = chrono::steady_clock::now();

    // Neighbor handler buffers add vehicle and set speed operations while the 
    // edge handling is going on in each barrier, apply them after to avoid
    // interference and then start again
    for (partId_t partId : neighborPartitions) {
      neighborClientHandlers[partId]->applyMutableOperations();
    }

    if (args.logMsgNum) {
      lock_guard<mutex> lock(msgCountLockIn);
      lock_guard<mutex> lock2(msgCountLockOut);
      ofstream(logMsgsFile, ios::app) << Simulation::getTime() << "," << msgCountIn << "," << msgCountOut << "\n";
      msgCountIn = 0;
      msgCountOut = 0;
    }

    // if (measureInteractTime) handleTime += chrono::steady_clock::now() - timeBefore;
  }

  if (measureSimTime) {
    double duration = duration_cast<chrono::milliseconds>(simTime).count() / 1000.0;
    log("Took {}s for simulation, writing to file...\n", duration);
    auto timeFile = filesystem::path(args.dataDir) / ("simtime" + to_string(id) + ".txt");
    ofstream(timeFile) << duration << endl;
  }
  if (measureInteractTime) {
    double duration = duration_cast<chrono::milliseconds>(commTime).count() / 1000.0;
    log("Took {}s for communication, writing to file...\n", duration);
    auto timeFile = filesystem::path(args.dataDir) / ("commtime" + to_string(id) + ".txt");
    ofstream(timeFile) << duration << endl;

    // double handleDuration = duration_cast<chrono::milliseconds>(handleTime).count() / 1000.0;
    // log("Took {}s for handling interactions, writing to file...\n", handleDuration);
    // auto timeFile2 = filesystem::path(args.dataDir) / ("handletime" + to_string(id) + ".txt");
    // ofstream(timeFile2) << handleDuration << endl;
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