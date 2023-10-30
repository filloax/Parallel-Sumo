/**
ParallelSim.cpp

Parallelizes a SUMO simulation. Partitions a SUMO network by number of threads,
and runs each parallel SUMO network partition in a PartitionManager.

Author: Phillip Taylor

Contributions: Filippo Lenzi
*/
#include "ParallelSim.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <fstream>
#include <ctime>
#include <nlohmann/json_fwd.hpp>
#include <ratio>
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
#include <chrono>
#include <thread>

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
using namespace chrono;

typedef std::unordered_multimap<string, int>::iterator umit;

ParallelSim::ParallelSim(string cfg, bool gui, int threads, Args& args) :
  cfgFile(cfg),
  numThreads(threads),
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
      std::cout << "No end time specified, will only check for empty partitions." << std::endl;
      endTime = -1;
    } else {
      endTime = std::stoi(endTimeEl->Attribute("value"));
    }
  }
  else {
    std::cout << "No end time specified, will only check for empty partitions." << std::endl;
    endTime = -1;
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

  vector<string> partitioningArgs {
    "scripts/createParts.py",
    "-N", std::to_string(numThreads),
    "-c", cfgFile,
    "--data-folder", args.dataDir
  };

  if (!metis) {
    partitioningArgs.push_back("--no-metis");
  }
  if (keepPoly) {
    partitioningArgs.push_back("--keep-poly");
  }
  if (args.partitioningThreads) {
    partitioningArgs.push_back("--threads");
    partitioningArgs.push_back(std::to_string(args.partitioningThreads));
  }
  
  if (args.partitioningArgs.size() > 0)
    partitioningArgs.insert(partitioningArgs.end(), args.partitioningArgs.begin(), args.partitioningArgs.end());

  std::cout << "Running createParts.py to split graph and create partition files..." << std::endl;

  auto time0 = high_resolution_clock::now();

  std::cout << std::endl << std::endl << ">>> ================================================== <<<" << std::endl << std::endl;
  runProcess(pythonCommand, partitioningArgs);

  int status;
  waitProcess(&status);
  std::cout << std::endl << std::endl << ">>> ================================================== <<<" << std::endl << std::endl;
  if(WEXITSTATUS(status)) {
    std::cout << "failed while partitioning" << std::endl;
    exit(EXIT_FAILURE);
  }
  printf("partitioning successful with status: %d\n", WEXITSTATUS(status));

  auto time1 = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(time1 - time0).count() / 1000.0;
  cout << "Partitioning took " << duration<< "ms!" << endl;
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

// Not reference so thread starts correctly
void ParallelSim::waitForPartitions(vector<pid_t> pids) {
  map<pid_t, partId_t> pidParts;
  for (int i = 0; i < pids.size(); i++) {
    pidParts[pids[i]] = i;
  }

  if (args.verbose)
    __printVector(pids, "Coordinator[t] | Started partition wait thread, pids are: ", ", ", true, std::cout);
  while (pids.size() > 0) {
    int status;
    pid_t pid = waitProcess(&status);
    if (pid == -1) {
      perror("waitProcess\n");
    } else if (pid == 0) {
      continue;
    } else {
      // A child process has exited
      if (endTime >= 0) {
      printf("Coordinator[t] | Partition %d [pid %d] exited with status %d at step %d/%d\n", pidParts[pid], pid, status, steps, endTime);
      } else {
      printf("Coordinator[t] | Partition %d [pid %d] exited with status %d at step %d\n", pidParts[pid], pid, status, steps);
      }
      pids.erase(std::remove(pids.begin(), pids.end(), pid), pids.end());

      if (status != 0) {
        if (!allFinished) {
          perror("Partition ended with an error!\n");
          for (auto pid : pids) {
            killProcess(pid);
          }

          exit(status);
        } else {
          perror("Partition ended with an error, but seemingly everything finished!\n");
        }
      }
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

  if (endTime >= 0) {
  std::cout << "Will end at time " << endTime << std::endl;
  } else {
  std::cout << "Will check empty partitions for end" << std::endl;
  }

  // Context for ZeroMQ message-passing, ideally one per program
  zmq::context_t zctx{1};

  // Now Python does this
  vector<pid_t> pids(numThreads);

  // create partitions
  for(partId_t i=0; i<numThreads; i++) {
    #ifndef PSUMO_SINGLE_EXECUTABLE
      // If in separate executables mode, 
      // create a new process with the partition
      // by launching its exe

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
      if (args.pinToCpu) partArgs.push_back("--pin-to-cpu");
      if (args.verbose)  partArgs.push_back("--verbose");
      if (args.sumoArgs.size() > 0)
        partArgs.insert(partArgs.end(), args.sumoArgs.begin(), args.sumoArgs.end());
      
      if (args.verbose)
        printf("Coordinator | Starting process for part %i\n", i);
      filesystem::path path;
      if (args.gui) {
        path = exeDir / PROGRAM_NAME_PART_GUI;
      } else {
        path = exeDir / PROGRAM_NAME_PART;
      }
      pid = runProcess(path, partArgs);
    #else

    printf("SINGLE EXE VER NOT SUPPORTED AS OF NOW; IS OLD VER\n");
    exit(EXIT_FAILURE);
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
  thread waitThread(&ParallelSim::waitForPartitions, this, pids);

  // From here, coordination process
  coordinatePartitionsSync(zctx);

  waitThread.join();
}

void ParallelSim::coordinatePartitionsSync(zmq::context_t& zctx) {
  if (args.verbose)
    printf("Coordinator | Starting coordinator routine...\n");

  // Initialize sockets used to sync partitions in a barrier-like fashion
  vector<zmq::socket_t*> sockets(numThreads);
  for (int i = 0; i < numThreads; i++) {
    string uri = psumo::getSyncSocketId(args.dataDir, i);
    try {
      sockets[i] = makeSocket(zctx, zmq::socket_type::rep);
      sockets[i]->bind(uri);
    } catch (zmq::error_t& e) {
      stringstream msg;
      msg << "Coordinator | ZMQ error in binding socket " << i << " to '" << uri
        << "': " << e.what() << "/" << e.num() << endl;
      cerr << msg.str();
      exit(-1);
    }
  }

  if (args.verbose)
    printf("Coordinator | Bound sockets\n");

  vector<zmq::pollitem_t> pollitems(numThreads);
  for (int i = 0; i < numThreads; i++) {
    // operator void* included in the ZeroMQ C++ wrapper, 
    // needed to make poll work as it operates on the C version
    pollitems[i].socket = castPollSocket(*sockets[i]);
    // pollitems[i].socket = *sockets[i]; // Should be equivalent, but let's use the operator
    pollitems[i].events = ZMQ_POLLIN;
  }

  vector<bool> partitionReachedBarrier(numThreads);
  vector<bool> partitionReachedStepBarrier(numThreads);
  vector<bool> partitionEmpty(numThreads);
  vector<bool> partitionStopped(numThreads);
  zmq::message_t message;

  for (int i = 0; i < numThreads; i++) {
    partitionReachedBarrier[i] = false;
    partitionReachedStepBarrier[i] = false;
    partitionEmpty[i] = false;
    partitionStopped[i] = false;
  }

  int barrierPartitions = 0;
  int stepPartitions = 0;
  int stoppedPartitions = 0;

  high_resolution_clock::time_point time0;
  bool setTime = false;

  steps = 0;
  syncBarrierTimes = 0;

  while (true) {
    zmq::poll(pollitems);

    for (int i = 0; i < numThreads; i++) {
      auto item = pollitems[i];
      // Message arrived on corresponding socket
      if (item.revents & ZMQ_POLLIN) {
        auto socket = sockets[i];
        auto result = socket->recv(message);
        int opcode;
        auto data = static_cast<char*>(message.data());
        std::memcpy(&opcode, data, sizeof(int));

        switch(opcode) {
          case SyncOps::BARRIER:
            if (!partitionReachedBarrier[i]) {
              partitionReachedBarrier[i] = true;
              barrierPartitions++;
              if (args.verbose)
                printf("Coordinator | Partition %d reached barrier (%d/%d)\n", i, barrierPartitions, numThreads);
            } else {
              stringstream msg;
              msg << "Partition sent reached barrier message twice! Is " << i << endl;
              cerr << msg.str();
              // Send message just incase, but this is undefined behavior
              socket->send(zmq::str_buffer("repeated"), zmq::send_flags::none);
            }
            break;

          case SyncOps::BARRIER_STEP:
            if (!partitionReachedStepBarrier[i]) {
              bool empty; 
              partitionReachedStepBarrier[i] = true;
              std::memcpy(&empty, data + sizeof(int), sizeof(bool));
              partitionEmpty[i] = empty;
              stepPartitions++;
              if (args.verbose)
                printf("Coordinator | Partition %d reached step barrier (%d/%d)\n", i, stepPartitions, numThreads);
            } else {
              stringstream msg;
              msg << "Partition sent reached step barrier message twice! Is " << i << endl;
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
      allFinished = true;
      break;
    }
    if (barrierPartitions >= numThreads) {
      if (args.verbose)
        printf("Coordinator | All partitions reached barrier\n");
      syncBarrierTimes++;
      barrierPartitions = 0;
      for (int i = 0; i < numThreads; i++) partitionReachedBarrier[i] = false;

      // All partitions reached barrier, reply to each to unlock it
      for (int i = 0; i < numThreads; i++) {
        sockets[i]->send(zmq::str_buffer("ok"), zmq::send_flags::none);
      }

      if (!setTime) {
        setTime = true;
        // start time at first barrier
        time0 = high_resolution_clock::now();
      }
    }
    if (stepPartitions >= numThreads) {
      if (args.verbose)
        printf("Coordinator | All partitions reached step barrier\n");
      steps++;
      stepPartitions = 0;
      for (int i = 0; i < numThreads; i++) partitionReachedStepBarrier[i] = false;

      bool allEmpty = true;
      for (bool empty : partitionEmpty) {
        if (!empty) {
          allEmpty = false;
          break;
        }
      }

      if (allEmpty) {
        if (args.verbose) {
          printf("Coordinator | All partitions empty after step\n");
        }
      }

      // All partitions reached barrier, reply to each to unlock it
      for (int i = 0; i < numThreads; i++) {
        zmq::message_t message(sizeof(bool));
        std::memcpy(message.data(), &allEmpty, sizeof(bool));
        sockets[i]->send(message, zmq::send_flags::none);
      }

      if (!setTime) {
        setTime = true;
        // start time at first barrier
        time0 = high_resolution_clock::now();
      }
    }
  }

  for (int i = 0; i < numThreads; i++) {
    sockets[i]->close();
    delete sockets[i];
  }

  high_resolution_clock::time_point time1 = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(time1 - time0).count() / 1000.0;
  cout << "Parallel simulation took " << duration << "ms!" << endl;
}