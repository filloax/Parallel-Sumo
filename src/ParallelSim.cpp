/**
ParallelSim.cpp

Parallelizes a SUMO simulation. Partitions a SUMO network by number of threads,
and runs each parallel SUMO network partition in a PartitionManager.

Author: Phillip Taylor
*/

#include <iostream>
#include <fstream>
#include <ctime>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <set>
#include "libs/tinyxml2.h"
#include "ParallelSim.hpp"
#include "utils.hpp"
#include "args.hpp"
#include <filesystem> // C++17
#include <barrier> // C++20
#include "SumoConnectionRouter.hpp"

namespace fs = std::filesystem;

typedef std::unordered_multimap<std::string, int>::iterator umit;

ParallelSim::ParallelSim(const std::string& host, int port, std::string cfg, bool gui, int threads, std::vector<std::string>& sumoArgs, Args& args) :
  host(host),
  port(port),
  cfgFile(cfg),
  numThreads(threads),
  sumoArgs(sumoArgs),
  args(args),
  dataFolder("data")
  {

  // set paths for sumo executable binaries
  std::string sumoExe;
  if(gui)
    sumoExe = "/bin/sumo-gui";
  else
    sumoExe = "/bin/sumo";

  std::string sumoPath;
  char* sumoPathPtr(getenv("SUMO_HOME"));
  if (sumoPathPtr == NULL) {
    std::cout << "$SUMO_HOME is not set! Must set $SUMO_HOME." << std::endl;
    exit(EXIT_FAILURE);
  }
  else {
    sumoPath = sumoPathPtr;
    std::cout << "$SUMO_HOME is set to '" << sumoPath << "'" << std::endl;
    SUMO_BINARY = sumoPath + sumoExe;
  }
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
  std::string cfgStr(cfgFile);
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
  std::string netText(netFileEl->Attribute("value"));
  netText = path+netText;
  netFile.assign(netText);
  // set routes-file
  std::string routeText(routeFileEl->Attribute("value"));
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

  std::vector<std::string> partArgs {
    pythonCommand, "scripts/createParts.py",
    "-n", std::to_string(numThreads),
    "-C", cfgFile,
    "--data-folder", dataFolder
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

  pid_t pid;
  int status;
  switch(pid = fork()) {
    case -1:
      // fork() has failed
      perror("fork");
      break;
    case 0:
      std::cout << "Running createParts.py to split graph and create partition files..." << std::endl;
      std::cout << "command: ";
      for (int i = 0; i < partArgs.size(); i++) std::cout << partArgs[i] << " ";
      std::cout << std::endl;
      EXECVP_CPP(partArgs);
      std::cerr << "execvp() for createParts.py has failed: " << errno << std::endl;
      exit(EXIT_FAILURE);
      break;
    default:
      // waiting for convertToMetis.py
      pid = wait(&status);
      if(WEXITSTATUS(status)) {
        std::cout << "failed while partitioning" << std::endl;
        exit(EXIT_FAILURE);
      }
      printf("partitioning successful with status: %d\n", WEXITSTATUS(status));
      break;
  }
}

void ParallelSim::loadRealNumThreads() {
  // Read actual partition num in case METIS had empty partitions
  std::ifstream partNumFile(dataFolder + "/numParts.txt"); // Open the input file

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

void ParallelSim::calcBorderEdges(std::vector<std::vector<border_edge_indices_t>>& borderEdgesIndices, std::vector<std::vector<int>>& partNeighbors){
  std::unordered_multimap<std::string, int> allEdges;
  std::vector<std::set<int>> partNeighborSets(numThreads);

  // add all edges to map, mapping edge ids to partition ids
  for(int i=0; i<numThreads; i++) {
    std::string currNetFile = dataFolder + "/part"+std::to_string(i)+".net.xml";
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
    std::string key = it->first;
    if(allEdges.count(key)>1) {
      std::pair<umit, umit> edgePair = allEdges.equal_range(key);
      umit edgeIt1 = edgePair.first;
      umit edgeIt2 = ++edgePair.first;
      border_edge_indices_t borderEdge1 = {};
      border_edge_indices_t borderEdge2 = {};
      borderEdge1.id = key;
      borderEdge2.id = key;
      std::string currNetFile = dataFolder + "/part"+std::to_string(edgeIt1->second)+".net.xml";
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
          const std::string fromJunc = el->Attribute("from");
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
      borderEdgesIndices[edgeIt1->second].push_back(borderEdge1);
      borderEdgesIndices[edgeIt2->second].push_back(borderEdge2);

      it = allEdges.erase(edgePair.first, edgePair.second);
    } else {
      it = allEdges.erase(it);
    }
  }

  for (int i = 0; i < numThreads; i++) {
    partNeighbors[i].insert(partNeighbors[i].begin(), partNeighborSets[i].begin(), partNeighborSets[i].end());
  }
}

void ParallelSim::startSim(){
  if (numThreads > 1)
    this->loadRealNumThreads();

  if (numThreads == 1) {
    std::cout << "Running in 1 thread mode, will use the original cfg (not intended? check your --num-threads param or the partitions created)" << std::endl;
  }

  std::string partCfg;
  std::vector<PartitionManager*> parts;
  std::vector<SumoConnectionRouter*> routers;

  std::cout << "Will end at time " << endTime << std::endl;

  std::barrier<> syncBarrier(numThreads);

  std::vector<std::vector<border_edge_indices_t>> borderEdgesIndices(numThreads);
  std::vector<std::vector<int>> partNeighbors(numThreads);
  calcBorderEdges(borderEdgesIndices, partNeighbors);

  // create partitions
  for(int i=0; i<numThreads; i++) {
    if (numThreads > 1)
      partCfg = dataFolder + "/part"+std::to_string(i)+".sumocfg";
    else // Only one thread, so use normal config, for the purpose of benchmarking comparisions
      partCfg = cfgFile;
    printf("Creating partition manager %d on cfg file %s, port=%d\n", i, partCfg.c_str(), port+i);

    // Memory shared in stack but not an issue as it gets used only in the constructor
    std::vector<partitionPort> partitionPorts(partNeighbors[i].size() + 1);
    partitionPorts[0] = {i, port+i};
    for (int j = 0; j < partNeighbors[i].size(); j++) {
      int neighborIdx = partNeighbors[i][j];
      partitionPorts[j+1] = {neighborIdx, port+neighborIdx};
    }

    SumoConnectionRouter* router = new SumoConnectionRouter(host, partitionPorts, numThreads, i);
    PartitionManager* part = new PartitionManager(SUMO_BINARY, i, syncBarrier, *router, partCfg, port+i, endTime, sumoArgs, args);
    parts.push_back(part);
    routers.push_back(router);
  }

  std::vector<std::vector<border_edge_t>> borderEdges(numThreads);

  // start parallel simulations
  for(int i=0; i<numThreads; i++) {
    for (int j=0; j<borderEdgesIndices[i].size(); j++) {
      borderEdges[i][j].id    = borderEdgesIndices[i][j].id;
      borderEdges[i][j].lanes = borderEdgesIndices[i][j].lanes;
      borderEdges[i][j].from  = parts[borderEdgesIndices[i][j].from];
      borderEdges[i][j].to    = parts[borderEdgesIndices[i][j].to];
    }
    parts[i]->setMyBorderEdges(borderEdges[i]);
    if(!parts[i]->startPartition()){
      printf("Error creating partition %d", i);
      exit(EXIT_FAILURE);
    }
  }
  // join all threads when finished executing
  for (int i=0; i<numThreads; i++) {
    parts[i]->waitForPartition();
  }
  
  for(int i=0; i<numThreads; i++) {
    delete parts[i];
    delete routers[i];
  }
}
