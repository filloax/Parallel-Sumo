cmake_minimum_required(VERSION 3.0.0)
project(ParallelSumo)

set(CMAKE_CXX_STANDARD 17)

# Use Clang as the compiler
set(CMAKE_CXX_COMPILER "clang++")

# Add source files
set(SRC_DIR src)
set(LIBS_DIR libs)

set(SOURCE_FILES
    ${SRC_DIR}/NeighborPartitionHandler.cpp
    ${SRC_DIR}/ParallelSim.cpp
    ${SRC_DIR}/PartitionEdgesStub.cpp
    ${SRC_DIR}/PartitionManager.cpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/utils.cpp
)

# Add library files
set(LIB_FILES
    ${LIBS_DIR}/argparse.hpp
    ${LIBS_DIR}/tinyxml2.cpp
)

# Include directories
include_directories(
    $ENV{SUMO_HOME}/include
    $ENV{SUMO_HOME}/src
    .
)

# Link directories and libraries
link_directories($ENV{SUMO_HOME}/bin)
link_libraries(sumocpp zmq)

add_executable(ParallelSumo ${SOURCE_FILES} ${LIB_FILES})

# Add header files for IDEs that support autocompletion
target_sources(ParallelSumo PRIVATE
    ${SRC_DIR}/NeighborPartitionHandler.hpp
    ${SRC_DIR}/ParallelSim.hpp
    ${SRC_DIR}/PartitionEdgesStub.hpp
    ${SRC_DIR}/PartitionManager.hpp
    ${SRC_DIR}/utils.hpp
)
