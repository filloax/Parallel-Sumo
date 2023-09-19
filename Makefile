CC=g++
CXXFLAGS= -std=c++11 -I. -g

all: clean main
clean:
	rm *.o || true
	rm ./main || true

main: main.o ParallelSim.o PartitionManager.o TraCIAPI.o socket.o storage.o Pthread_barrier.o tinyxml2.o