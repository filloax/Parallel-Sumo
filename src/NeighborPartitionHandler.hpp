/**
NeighborPartitionHandler.hpp

Class definition for NeighborPartitionHandler.

Handles incoming messages from neighboring partitions, either responding with state
or queueing modifying operations (like adding vehicles).

Author: Filippo Lenzi
*/

#pragma once

#include <vector>
#include <zmq.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

class NeighborPartitionHandler;

#include "PartitionManager.hpp"

typedef struct {
  std::string& vehId;
  double speed;
} set_veh_speed_t;

typedef struct {
  std::string& vehId;
  std::string& routeId; 
  std::string& vehType;
  std::string& laneId;
  int laneIndex;
  double lanePos;
  double speed;
} add_veh_t;

/**
Handle the requests from other partitions; immediately reply 
to getter requests (currently only getVehiclesOnEdge), queue
up muting methods. Will immediately reply to setters too
due to the Req/Rep model in ZeroMQ.
*/
class NeighborPartitionHandler {
private:
  zmq::context_t* zcontext; // Separate context to handle stuff while partition manager waits for barrier
  zmq::socket_t socket;
  const int clientId;
  PartitionManager& owner;
  const std::string socketUri;
  bool listening; // Start listening logic, check if still going
  bool stop_; // Stop listening logic, but not thread
  bool term; // Stop listening thread
  std::thread listenThread;
  std::mutex operationsBufferLock;
  std::mutex secondThreadSignalLock;
  std::condition_variable secondThreadCondition;

  std::vector<set_veh_speed_t> setSpeedQueue;
  std::vector<add_veh_t> addVehicleQueue;

  void listenCheck();
  void listenThreadLogic();

  bool handleGetEdgeVehicles(zmq::message_t& request);
  bool handleSetVehicleSpeed(zmq::message_t& request);
  bool handleAddVehicle(zmq::message_t& request);
  bool handleStepEnd(zmq::message_t& request);
public:
  NeighborPartitionHandler(PartitionManager& owner, int clientId);
  ~NeighborPartitionHandler();

  void join();

  void start();
  void stop();

  void listenOn();
  void listenOff();

  // Ideally call these when the listen thread is idle, on the main thread
  void applyMutableOperations();
};
