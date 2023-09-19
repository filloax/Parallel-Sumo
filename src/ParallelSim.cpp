/**
ParallelSim.cpp

Parallelizes a SUMO simulation. Partitions a SUMO network by number of threads,
and runs each parallel SUMO network partition in a PartitionManager.

Author: Phillip Taylor
*/

#include <iostream>
#include <fstream>
#include <pthread.h>
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
#include "libs/Pthread_barrier.h"
#include "libs/tinyxml2.h"
#include "ParallelSim.h"
#include "utils.h"


typedef std::unordered_multimap<std::string, int>::iterator umit;

ParallelSim::ParallelSim(const std::string& host, int port, const char* cfg, bool gui, int threads, std::vector<std::string> extraArgs) :
  host(host),
  port(port),
  cfgFile(cfg),
  numThreads(threads),
  extraArgs(extraArgs) {
  dataFolder = "data";

  // set paths for sumo executable binaries
  const char* sumoExe;
  if(gui)
    sumoExe = "/bin/sumo-gui";
  else
    sumoExe = "/bin/sumo";

  char* sumoPath(getenv("SUMO_HOME"));
  if (sumoPath == NULL) {
    std::cout << "$SUMO_HOME is not set! Must set $SUMO_HOME." << std::endl;
    exit(EXIT_FAILURE);
  }
  else {
    std::cout << "$SUMO_HOME is set to '" << sumoPath << "'" << std::endl;
    int len = strlen(sumoPath);
    char* tmp1 = new char[len+14];
    char* tmp2 = new char[len+16];
    char* tmp3 = new char[len+26];
    strcpy(tmp1, sumoPath);
    strcpy(tmp2, sumoPath);
    strcpy(tmp3, sumoPath);
    SUMO_BINARY = strcat(tmp1, sumoExe);
    NETCONVERT_BINARY = strcat(tmp2, "/bin/netconvert");
    CUT_ROUTES_SCRIPT = strcat(tmp3, "/tools/route/cutRoutes.py");
   }
   // get end time
   tinyxml2::XMLDocument cfgDoc;
   tinyxml2::XMLError e = cfgDoc.LoadFile(cfgFile);
   if(e) {
     std::cout << cfgDoc.ErrorIDToName(e) << std::endl;
     exit(EXIT_FAILURE);
   }
   tinyxml2::XMLElement* cfgEl = cfgDoc.FirstChildElement("configuration");
   if (cfgEl == nullptr) {
     std::cout << "sumo config error: no configuration" << std::endl;
     exit(EXIT_FAILURE);
   }
   tinyxml2::XMLElement* endTimeEl = cfgEl->FirstChildElement("time")->FirstChildElement("end");
   if (endTimeEl == nullptr) {
     std::cout << "No end time specified. Setting default end time at 1000 steps." << std::endl;
     endTime = 1000;
   }
   else {
     endTime = atoi(endTimeEl->Attribute("value"));
   }
}

void ParallelSim::getFilePaths(){
  // get paths for net and route files
  std::string cfgStr(cfgFile);
  int found = cfgStr.find_last_of("/\\");
  path = cfgStr.substr(0,found+1);
  // load sumo cfg file
  tinyxml2::XMLDocument cfgDoc;
  tinyxml2::XMLError e = cfgDoc.LoadFile(cfgFile);
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

void ParallelSim::partitionNetwork(bool metis){
  // Filippo Lenzi: Originally was implemented in C++
  // in the original program, but because of it not needing to be
  // perfectly optimized (as partitioning is run once per simulation configuration)
  // it was converted to a Python script
  std::string numThreadsStr = std::to_string(numThreads);
  std::vector<std::string> args {
    "python", "scripts/createParts.py",
    "-n", numThreadsStr,
    "-C", cfgFile,
    "-N", netFile,
    "-R", routeFile,
    "--data-folder", dataFolder
  };

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
      for (int i = 0; i < args.size(); i++) std::cout << args[i] << " ";
      std::cout << std::endl;
      EXECVP_CPP(args);
      std::cerr << "execvp() for convertToMetis.py has failed: " << errno << std::endl;
      exit(EXIT_FAILURE);
      break;
    default:
      // waiting for convertToMetis.py
      pid = wait(&status);
      if(WEXITSTATUS(status)) {
        std::cout << "failed while converting to metis" << std::endl;
        exit(EXIT_FAILURE);
      }
      printf("metis partitioning successful with status: %d\n", WEXITSTATUS(status));
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

void ParallelSim::setBorderEdges(std::vector<border_edge_t> borderEdges[], std::vector<PartitionManager*>& parts){
  std::unordered_multimap<std::string, int> allEdges;
  // add all edges to map, mapping edge ids to partition ids
  for(int i=0; i<parts.size(); i++) {
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
      border_edge_t borderEdge1 = {};
      border_edge_t borderEdge2 = {};
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
          const char* fromJunc = el->Attribute("from");
          PartitionManager* from;
          PartitionManager* to;
          for(tinyxml2::XMLElement* junEl = netEl->FirstChildElement("junction"); junEl != NULL; junEl = junEl->NextSiblingElement("junction")) {
            if(strcmp(fromJunc, junEl->Attribute("id"))==0) {
              if(strcmp(junEl->Attribute("type"), "dead_end")==0) {
                from = parts[edgeIt2->second];
                to = parts[edgeIt1->second];
              }
              else {
                from = parts[edgeIt1->second];
                to = parts[edgeIt2->second];
              }
              borderEdge1.from = from;
              borderEdge2.from = from;
              borderEdge1.to = to;
              borderEdge2.to = to;
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
}

void ParallelSim::startSim(){
  this->loadRealNumThreads();

  std::string cfg;
  std::vector<PartitionManager*> parts;
  std::vector<border_edge_t> borderEdges[numThreads];
  pthread_mutex_t lock;
  pthread_barrier_struct barrier;
  pthread_cond_t cond;

  // create partitions
  pthread_mutex_init(&lock, NULL);
  pthread_cond_init(&cond, NULL);
  pthread_barrier_init(&barrier, NULL, numThreads);
  for(int i=0; i<numThreads; i++) {
    cfg = dataFolder + "/part"+std::to_string(i)+".sumocfg";
    printf("Creating partition manager %d on cfg file %s, port=%d\n", i, cfg.c_str(), port+i);
    PartitionManager* part = new PartitionManager(SUMO_BINARY, i, &barrier, &lock, &cond, cfg, host, port+i, endTime, extraArgs);
    parts.push_back(part);
  }

  setBorderEdges(borderEdges, parts);
  // start parallel simulations
  for(int i=0; i<numThreads; i++) {
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

  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&lock);
  pthread_barrier_destroy(&barrier);
  for(int i=0; i<numThreads; i++) {
    delete parts[i];
  }
}
