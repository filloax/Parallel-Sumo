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

namespace psumo {
  class NeighborPartitionHandler;
}
#include "PartitionManager.hpp"

namespace psumo {

static const size_t OPERATION_QUEUE_SIZE = 512;

typedef struct {
  std::string vehId;
  double speed;
} set_veh_speed_t;

typedef struct {
  std::string vehId;
  std::string routeId; 
  std::string vehType;
  std::string laneId;
  int laneIndex;
  double lanePos;
  double speed;
} add_veh_t;

template <typename T, int N> class OperationQueue {
  public:
  std::array<T, N> queue;
  int currentSize;

  bool append(T el) {
    if (currentSize < N) {
      queue[currentSize] = el;
      currentSize++;
      return true;
    }
    return false;
  }

  void clear() {
    currentSize = 0;
  }
};

/**
Handle the requests from other partitions; immediately reply 
to getter requests (currently only getVehiclesOnEdge), queue
up muting methods. Will immediately reply to setters too
due to the Req/Rep model in ZeroMQ.
*/
class NeighborPartitionHandler {
private:
  zmq::context_t& zcontext; // Separate context to handle stuff while partition manager waits for barrier
  zmq::socket_t socket;
  zmq::socket_t controlSocketMain;
  zmq::socket_t controlSocketThread;
  const int clientId;
  PartitionManager& owner;
  const std::string socketUri;
  bool threadWaiting;
  bool listening; // Start listening logic, check if still going
  bool stop_; // Stop listening logic, but not thread
  bool term; // Stop listening thread
  std::thread listenThread;
  std::mutex operationsBufferLock;
  std::mutex secondThreadSignalLock;
  std::condition_variable secondThreadCondition;

  OperationQueue<set_veh_speed_t, OPERATION_QUEUE_SIZE> setSpeedQueue;
  OperationQueue<add_veh_t, OPERATION_QUEUE_SIZE> addVehicleQueue;

  void listenCheck();
  void listenThreadLogic();

  bool handleGetEdgeVehicles(zmq::message_t& request);
  bool handleHasVehicle(zmq::message_t& request);
  bool handleHasVehicleInEdge(zmq::message_t& request);
  bool handleSetVehicleSpeed(zmq::message_t& request);
  bool handleAddVehicle(zmq::message_t& request);

  template<typename... _Args > 
    void log(std::format_string<_Args...>  format, _Args&&... args);
  template<typename... _Args > 
    void logerr(std::format_string<_Args...>  format, _Args&&... args);
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

}