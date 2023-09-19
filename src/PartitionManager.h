/**
PartitionManager.h

Class definition for PartitionManager.

Author: Phillip Taylor
*/


#ifndef PARTITIONMANAGER_INCLUDED
#define PARTITIONMANAGER_INCLUDED

#include <cstdlib>
#include <pthread.h>
#include <string>
#include <vector>
#include "libs/Pthread_barrier.h"
#include "libs/TraCIAPI.h"

typedef struct border_edge_t border_edge_t;

class PartitionManager {
  private:
    const char* SUMO_BINARY;
    int id;
    std::vector<border_edge_t> toBorderEdges;
    std::vector<border_edge_t> fromBorderEdges;
    std::string cfg;
    std::string host;
    int port;
    int endT;
    std::vector<std::string> extraArgs;
    std::string dataFolder = "";
    bool synching = false;
    bool waiting = false;
    pthread_t myThread;
    pthread_barrier_struct* barrierAddr;
    pthread_mutex_t* lockAddr;
    pthread_cond_t* condAddr;
    TraCIAPI myConn;
    // thread helper function
    static void * internalSimFunc(void* This){
      ((PartitionManager*)This)->internalSim();
      return NULL;
    }
    // handle border edges where vehicles are incoming
    void handleToEdges(int, std::vector<std::string>[]);
    // handle border edges where vehicles are outgoing
    void handleFromEdges(int, std::vector<std::string>[]);

  protected:
    // start sumo simulation in thread
    virtual void internalSim();

public:
   // params: sumo binary, id, barrier, lock, cond, sumo config, host, port, end time
   PartitionManager(const char* binary, int id, pthread_barrier_struct* barr,
    pthread_mutex_t* lock, pthread_cond_t* cond, std::string& cfg, std::string& host, int port, int t,
    std::vector<std::string> extraArgs);
  // set this partition's border edges
   void setMyBorderEdges(std::vector<border_edge_t>);
   /* Starts this partition in a thread. Returns true if the thread was
      successfully started, false if there was an error starting the thread */
   bool startPartition();
   // Will not return until the internal thread has exited
   void waitForPartition();
   // connect to TraCI object
   void connect();
   // get vehicles on edge
   std::vector<std::string> getEdgeVehicles(const std::string&);
   // get edges of route
   std::vector<std::string> getRouteEdges(const std::string&);
   // add vehicle into simulation
   void add(const std::string&, const std::string&, const std::string&,
     const std::string&, const std::string&, const std::string&);
   // move vehicle to specified position on lane
   void moveTo(const std::string&, const std::string&, double);
   // set vehicle speed to propagate traffic conditions in next partition
   void slowDown(const std::string&, double);
   // set synching boolean
   void setSynching(bool);
   // return true if partition is synching, false if not
   bool isSynching();
   // return true if partition is waiting, false if not
   bool isWaiting();
   // wait for synch to resume simulation
   void waitForSynch();
   // close TraCI connection, exit from thread
   void closePartition();

};

struct border_edge_t {
    std::string id;
    std::vector<std::string> lanes;
    PartitionManager* from;
    PartitionManager* to;
};

#endif
