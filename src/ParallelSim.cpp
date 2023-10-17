/**
ParallelSim.cpp

Parallelizes a SUMO simulation. Partitions a SUMO network by number of threads,
and runs each parallel SUMO network partition in a PartitionManager.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/
#include "ParallelSim.hpp"

#include <cstring>
#include <exception>
#include <iostream>
#include <fstream>
#include <ctime>
#include <nlohmann/json_fwd.hpp>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <iterator>
#include <sys/types.h>
#include <unordered_map>
#include <vector>
#include <set>
#include <filesystem> // C++17

#include <zmq.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

#include "messagingShared.hpp"
#include "libs/tinyxml2.h"
#include "globals.hpp"
#include "utils.hpp"
#include "args.hpp"
#include "psumoTypes.hpp"

#ifdef PSUMO_SINGLE_EXECUTABLE
  #include <unistd.h>
  #include "PartitionManager.hpp"
#endif

namespace fs = std::filesystem;

using namespace std;
using namespace psumo;

typedef std::unordered_multimap<string, int>::iterator umit;

ParallelSim::ParallelSim(string cfg, bool gui, int threads, vector<string>& sumoArgs, Args& args) :
  cfgFile(cfg),
  numThreads(threads),
  sumoArgs(sumoArgs),
  args(args)
  {

  #ifdef PSUMO_SINGLE_EXECUTABLE
    cout << "ParallelSim | Creating in single executable mode!" << endl;
  #endif

  // set paths for sumo executable binaries
  #ifdef PSUMO_SINGLE_EXECUTABLE
  SUMO_BINARY = getSumoPath(gui);
  #endif

  // get end time
  tinyxml2::XMLDocument cfgDoc;
  tinyxml2::XMLError e = cfgDoc.LoadFile(cfgFile.c_str());
  if(e) {
    std::cout << cfgDoc.ErrorIDToName(e) << std::endl;
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* cfgEl = cfgDoc.FirstChildElement("configuration");
  if (cfgEl == nullptr) {
    std::cout << "sumo config error: no configuration" << std::endl;
    exit(EXIT_FAILURE);
  }
  tinyxml2::XMLElement* timeEl = cfgEl->FirstChildElement("time");
  if (timeEl != nullptr) {
    tinyxml2::XMLElement* endTimeEl = cfgEl->FirstChildElement("time")->FirstChildElement("end");
    if (endTimeEl == nullptr) {
      std::cout << "No end time specified. Setting default end time at 1000 steps." << std::endl;
      endTime = 1000;
    } else {
      endTime = std::stoi(endTimeEl->Attribute("value"));
    }
  }
  else {
    std::cout << "No end time specified. Setting default end time at 1000 steps." << std::endl;
    endTime = 1000;
  }
}

void ParallelSim::getFilePaths(){
  // get paths for net and route files
  string cfgStr(cfgFile);
  int found = cfgStr.find_last_of("/\\");
  path = cfgStr.substr(0,found+1);
  // load sumo cfg file
  tinyxml2::XMLDocument cfgDoc;
  tinyxml2::XMLError e = cfgDoc.LoadFile(cfgFile.c_str());
  if(e) {
    std::cout << cfgDoc.ErrorIDToName(e) << std::endl;
    exit(EXIT_FAILURE);
  }

  tinyxml2::XMLElement* cfgEl = cfgDoc.FirstChildElement("configuration");
  if (cfgEl == nullptr) {
    std::cout << "sumo config error: no configuration" << std::endl;
    exit(EXIT_FAILURE);
  }
  // get net-file
  tinyxml2::XMLElement* netFileEl = cfgEl->FirstChildElement("input")->FirstChildElement("net-file");
  if (netFileEl == nullptr) {
    std::cout << "sumo config error: no net-file" << std::endl;
    exit(EXIT_FAILURE);
  }
  // get routes-file
  tinyxml2::XMLElement* routeFileEl = cfgEl->FirstChildElement("input")->FirstChildElement("route-files");
  if (netFileEl == nullptr) {
    std::cout << "sumo config error: no route-files" << std::endl;
    exit(EXIT_FAILURE);
  }

  // set net-file
  string netText(netFileEl->Attribute("value"));
  netText = path+netText;
  netFile.assign(netText);
  // set routes-file
  string routeText(routeFileEl->Attribute("value"));
  routeText= path+routeText;
  routeFile.assign(routeText);

}

void ParallelSim::partitionNetwork(bool metis, bool keepPoly){
  // Filippo Lenzi: Originally was implemented in C++
  // in the original program, but because of it not needing to be
  // perfectly optimized (as partitioning is run once per simulation configuration)
  // it was converted to a Python script

  char* pythonPath = std::getenv("PYTHONPATH");
  fs::path pythonCommand = "python";
  if (pythonPath != nullptr) {
    fs::path pythonPathStr(pythonPath);
    std::cout << "PYTHONPATH set to " << pythonPathStr << ", using it" << std::endl;
    pythonCommand = pythonPathStr / pythonCommand;
  }

  vector<string> partArgs {
    "scripts/createParts.py",
    "-n", std::to_string(numThreads),
    "-C", cfgFile,
    "--data-folder", args.dataDir
  };

  if (!metis) {
    partArgs.push_back("--no-metis");
  }
  if (keepPoly) {
    partArgs.push_back("--keep-poly");
  }
  if (args.partitioningThreads) {
    partArgs.push_back("--threads");
    partArgs.push_back(std::to_string(args.partitioningThreads));
  }

  std::cout << "Running createParts.py to split graph and create partition files..." << std::endl;

  runProcess(pythonCommand, partArgs);

  int status;
  waitProcess(&status);
  if(WEXITSTATUS(status)) {
    std::cout << "failed while partitioning" << std::endl;
    exit(EXIT_FAILURE);
  }
  printf("partitioning successful with status: %d\n", WEXITSTATUS(status));
}

void ParallelSim::loadRealNumThreads() {
  // Read actual partition num in case METIS had empty partitions
  std::ifstream partNumFile(args.dataDir + "/numParts.txt"); // Open the input file

  if (partNumFile.is_open()) {
    int number;
    if (partNumFile >> number) {
      numThreads = number;
      printf("Set numThreads to %d from METIS output\n", numThreads);
    } else {
      std::cerr << "Failed to read metis output partition num from file." << std::endl;
    }

    partNumFile.close(); // Close the file
  } else {
    std::cerr << "Failed to open metis output partition num file." << std::endl;
  }
}

void ParallelSim::calcBorderEdges(vector<vector<border_edge_t>>& borderEdges, vector<vector<partId_t>>& partNeighbors){
  std::unordered_multimap<string, int> allEdges;
  vector<std::set<partId_t>> partNeighborSets(numThreads);

  // add all edges to map, mapping edge ids to partition ids
  for(int i=0; i<numThreads; i++) {
    string currNetFile = args.dataDir + "/part"+std::to_string(i)+".net.xml";
    tinyxml2::XMLDocument currNet;
    tinyxml2::XMLError e = currNet.LoadFile(currNetFile.c_str());
    tinyxml2::XMLElement* netEl = currNet.FirstChildElement("net");
    // insert all non-internal edges into map
    for(tinyxml2::XMLElement* el = netEl->FirstChildElement("edge"); el != NULL; el = el->NextSiblingElement("edge")) {
      if(el->Attribute("function") == nullptr || strcmp(el->Attribute("function"), "internal")!=0)
        allEdges.insert({el->Attribute("id"), i});
    }
  }
  // find border edges
  umit it = allEdges.begin();
  while(it != allEdges.end()) {
    string key = it->first;
    if(allEdges.count(key)>1) {
      std::pair<umit, umit> edgePair = allEdges.equal_range(key);
      umit edgeIt1 = edgePair.first;
      umit edgeIt2 = ++edgePair.first;
      border_edge_t borderEdge1 = {};
      border_edge_t borderEdge2 = {};
      borderEdge1.id = key;
      borderEdge2.id = key;
      string currNetFile = args.dataDir + "/part"+std::to_string(edgeIt1->second)+".net.xml";
      tinyxml2::XMLDocument currNet;
      tinyxml2::XMLError e = currNet.LoadFile(currNetFile.c_str());
      tinyxml2::XMLElement* netEl = currNet.FirstChildElement("net");
      // find edge in net file to get attributes
      for(tinyxml2::XMLElement* el = netEl->FirstChildElement("edge"); el != NULL; el = el->NextSiblingElement("edge")) {
        if((edgeIt1->first).compare(el->Attribute("id"))==0) {
          // get lanes
          for(tinyxml2::XMLElement* laneEl = el->FirstChildElement("lane"); laneEl != NULL; laneEl = laneEl->NextSiblingElement("lane")) {
            (borderEdge1.lanes).push_back(laneEl->Attribute("id"));
            (borderEdge2.lanes).push_back(laneEl->Attribute("id"));
          }
          // determine from and to partitions -  find junction to determine if dead end
          const string fromJunc = el->Attribute("from");
          int from;
          int to;
          for(tinyxml2::XMLElement* junEl = netEl->FirstChildElement("junction"); junEl != NULL; junEl = junEl->NextSiblingElement("junction")) {
            if(strcmp(fromJunc.c_str(), junEl->Attribute("id"))==0) {
              if(strcmp(junEl->Attribute("type"), "dead_end")==0) {
                from = edgeIt2->second;
                to = edgeIt1->second;
              }
              else {
                from = edgeIt1->second;
                to = edgeIt2->second;
              }
              borderEdge1.from = from;
              borderEdge2.from = from;
              borderEdge1.to = to;
              borderEdge2.to = to;
              
              // Insert explored parts to the set (unique values, so only get added once)
              partNeighborSets[from].insert(to);
              partNeighborSets[to].insert(from);

              break;
            }
          }
          break;
        }
      }
      borderEdges[edgeIt1->second].push_back(borderEdge1);
      borderEdges[edgeIt2->second].push_back(borderEdge2);

      it = allEdges.erase(edgePair.first, edgePair.second);
    } else {
      it = allEdges.erase(it);
    }
  }

  for (int i = 0; i < numThreads; i++) {
    partNeighbors[i].insert(partNeighbors[i].begin(), partNeighborSets[i].begin(), partNeighborSets[i].end());
  }
}

// Not reference so thread starts correctly
void waitForPartitions(vector<pid_t> pids) {
  __printVector(pids, "Coordinator[t] | Started partition wait thread, pids are: ", ", ", false, std::cout);
  while (pids.size() > 0) {
    int status;
    pid_t pid = waitProcess(&status);
    if (pid == -1) {
      perror("waitProcess\n");
    } else if (pid == 0) {
      continue;
    } else {
      // A child process has exited
      printf("Coordinator[t] | Child process %d exited with status %d\n", pid, status);
      pids.erase(std::remove(pids.begin(), pids.end(), pid), pids.end());
    }
  }
}

void ParallelSim::startSim(){
  if (numThreads > 1)
    loadRealNumThreads();

  if (numThreads == 1) {
    std::cout << "Running in 1 thread mode, will use the original cfg (not intended? check your --num-threads param or the partitions created)" << std::endl;
  }

  string partCfg;

  std::cout << "Will end at time " << endTime << std::endl;

  // Context for ZeroMQ message-passing, ideally one per program
  zmq::context_t zctx{1};

  vector<vector<border_edge_t>> borderEdges(numThreads);
  vector<vector<int>> partNeighbors(numThreads);
  calcBorderEdges(borderEdges, partNeighbors);

  vector<pid_t> pids(numThreads);

  // create partitions
  for(partId_t i=0; i<numThreads; i++) {
    #ifndef PSUMO_SINGLE_EXECUTABLE
      // If in separate executables mode, 
      // create a new process with the partition
      // by launching its exe

      // Pass data needed for the partition constructor as json
      nlohmann::json partData;
      partData["id"] = i;
      partData["neighbors"] = partNeighbors[i];
      // nlohmann::json handles the conversion, see psumoTypes.cpp
      partData["borderEdges"] = borderEdges[i];
      std::ofstream out(getPartitionDataFile(args.dataDir, i));
      out << partData.dump(2);
      out.close();

      pid_t pid;

      auto exeDir = getCurrentExeDirectory();
      vector<string> partArgs ({
        "-P", to_string(i),
        "-T", to_string(endTime),
        // Base ParallelSumo args, not all needed by partition
        // but pass everything for consistency
        "-N", to_string(args.numThreads),
        "-c", args.cfg,
        "--part-threads", to_string(args.partitioningThreads),
        "--data-dir", args.dataDir
      });
      if (args.gui) partArgs.push_back("--gui");
      if (args.skipPart) partArgs.push_back("--skip-part");
      if (args.keepPoly) partArgs.push_back("--keep-poly");
      printf("Coordinator | Starting process for part %i\n", i);
      pid = runProcess(exeDir / PROGRAM_NAME_PART, partArgs);
    #else

    // If in single executable mode, fork and create the partition here
    // (still in a new process)

    pid = fork();
    if (pid == 0) {
      printf("Creating partition manager %d on cfg file %s\n", i, partCfg.c_str());

        if (numThreads > 1)
          partCfg = args.dataDir + "/part"+std::to_string(i)+".sumocfg";
        else // Only one thread, so use normal config, for the purpose of benchmarking comparisions
          partCfg = cfgFile;

        PartitionManager part(
          SUMO_BINARY, i, partCfg, endTime, 
          partNeighbors[i], zctx, numThreads,
          sumoArgs, args
        );
        part.setMyBorderEdges(borderEdges[i]);
        part.startPartitionLocalProcess();
        printf("Finished partition %d\n", i);
        exit(0);
    } else {
    #endif

      printf("Created partition %d on pid %d\n", i, pid);
      pids[i] = pid;
      // if needed, add pid to a partition pids list later
    
    #ifdef PSUMO_SINGLE_EXECUTABLE
    }
    #endif
  }

  // Check for partition pids in case of unexpected exit in a subthread
  thread waitThread(&waitForPartitions, pids);

  // From here, coordination process

  coordinatePartitionsSync(zctx);

  waitThread.join();
}

void ParallelSim::coordinatePartitionsSync(zmq::context_t& zctx) {
  printf("Coordinator | Starting coordinator routine...\n");

  // Initialize sockets used to sync partitions in a barrier-like fashion
  vector<zmq::socket_t*> sockets(numThreads);
  for (int i = 0; i < numThreads; i++) {
    string uri = psumo::getSyncSocketId(args.dataDir, i);
    try {
      sockets[i] = new zmq::socket_t{zctx, zmq::socket_type::rep};
      sockets[i]->set(zmq::sockopt::linger, 0 );
      sockets[i]->bind(uri);
    } catch (zmq::error_t& e) {
      stringstream msg;
      msg << "Coordinator | ZMQ error in binding socket " << i << " to '" << uri
        << "': " << e.what() << "/" << e.num() << endl;
      cerr << msg.str();
      exit(-1);
    }
  }

  printf("Coordinator | Bound sockets\n");

  vector<zmq::pollitem_t> pollitems(numThreads);
  for (int i = 0; i < numThreads; i++) {
    // operator void* included in the ZeroMQ C++ wrapper, 
    // needed to make poll work as it operates on the C version
    pollitems[i].socket = sockets[i]->operator void*();
    // pollitems[i].socket = *sockets[i]; // Should be equivalent, but let's use the operator
    pollitems[i].events = ZMQ_POLLIN;
  }

  vector<bool> partitionReachedBarrier(numThreads);
  vector<bool> partitionStopped(numThreads);
  zmq::message_t message;

  for (int i = 0; i < numThreads; i++) {
    partitionReachedBarrier[i] = false;
    partitionStopped[i] = false;
  }

  int barrierPartitions = 0;
  int stoppedPartitions = 0;

  while (true) {
    zmq::poll(pollitems);

    for (int i = 0; i < numThreads; i++) {
      auto item = pollitems[i];
      // Message arrived on corresponding socket
      if (item.revents & ZMQ_POLLIN) {
        auto socket = sockets[i];
        auto result = socket->recv(message);
        int opcode;
        std::memcpy(&opcode, message.data(), sizeof(int));

        switch(opcode) {
          case SyncOps::BARRIER:
          if (!partitionReachedBarrier[i]) {
            partitionReachedBarrier[i] = true;
            barrierPartitions++;
            printf("Coordinator | Partition %d reached barrier (%d/%d)\n", i, barrierPartitions, numThreads); // TEMP
          } else {
            stringstream msg;
            msg << "Partition sent reached barrier message twice! Is " << i << endl;
            cerr << msg.str();
            // Send message just incase, but this is undefined behavior
            socket->send(zmq::str_buffer("repeated"), zmq::send_flags::none);
          }
          break;

          case SyncOps::FINISHED:
          if (!partitionStopped[i]) {
            partitionStopped[i] = true;
            stoppedPartitions++;
            // Partition stopping doesn't block signaling partition, so immediately respond
            sockets[i]->send(zmq::str_buffer("ok"), zmq::send_flags::none);
          } else {
            stringstream msg;
            msg << "Partition sent finished message twice! Is " << i << endl;
            cerr << msg.str();
            // Send message just incase, but this is undefined behavior
            socket->send(zmq::str_buffer("repeated"), zmq::send_flags::none);
          }
          break;
        }
      }
    }

    if (stoppedPartitions >= numThreads) {
      break;
    }
    if (barrierPartitions >= numThreads) {
      printf("Coordinator | All partitions reached barrier\n");
      barrierPartitions = 0;
      for (int i = 0; i < numThreads; i++) partitionReachedBarrier[i] = false;

      // All partitions reached barrier, reply to each to unlock it
      for (int i = 0; i < numThreads; i++) {
        sockets[i]->send(zmq::str_buffer("ok"), zmq::send_flags::none);
      }
    }
  }

  for (int i = 0; i < numThreads; i++) {
    delete sockets[i];
  }
}