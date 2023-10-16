cmake_minimum_required(VERSION 3.0.0)
project(ParallelSumo)

set(CMAKE_CXX_STANDARD 17)

option(PSUMO_SINGLE_EXECUTABLE "Create a single executable 
    using fork instead of making partitions a separate exe
    (currently doesn't work)" OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Check for Clang compiler
find_program(CLANG_COMPILER clang++)
if(CLANG_COMPILER)
    message(STATUS "Found Clang compiler: ${CLANG_COMPILER}")
    set(CMAKE_CXX_COMPILER ${CLANG_COMPILER})
else()
    message(WARNING "Clang compiler not found. Using default C++ compiler.")
endif()

# Add source files
set(SRC_DIR src)
set(LIBS_DIR libs)

set(SOURCE_FILES_COORDINATOR
    ${SRC_DIR}/ParallelSim.cpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/utils.cpp
    ${SRC_DIR}/messagingShared.cpp
    ${SRC_DIR}/psumoTypes.hpp
    ${SRC_DIR}/globals.hpp
    ${SRC_DIR}/main.cpp
)

set(SOURCE_FILES_PARTITION
    ${SRC_DIR}/NeighborPartitionHandler.cpp
    ${SRC_DIR}/PartitionEdgesStub.cpp
    ${SRC_DIR}/PartitionManager.cpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/partArgs.hpp
    ${SRC_DIR}/utils.cpp
    ${SRC_DIR}/messagingShared.cpp
    ${SRC_DIR}/psumoTypes.hpp
    ${SRC_DIR}/globals.hpp
    ${SRC_DIR}/partitionMain.cpp
)

# Add library files
set(LIB_FILES
    ${LIBS_DIR}/tinyxml2.cpp
)

message(STATUS "SUMO_HOME=$ENV{SUMO_HOME}")

# Include directories
include_directories(
    .
)

add_executable(ParallelSumo ${SOURCE_FILES_COORDINATOR} ${LIB_FILES})
if (NOT PSUMO_SINGLE_EXECUTABLE)
    add_executable(ParallelSumo-Partition ${SOURCE_FILES_PARTITION} ${LIB_FILES})
    message(STATUS "Added partition executable")
endif()


# ## load in pkg-config support
# find_package(PkgConfig)
# ## use pkg-config to get hints for 0mq locations
# pkg_check_modules(PC_ZeroMQ QUIET zmq)

#find cppzmq wrapper, installed by make of cppzmq
find_package(cppzmq)
message(STATUS "ZMQ CPP dir: " ${cppzmq_INCLUDE_DIR})
message(STATUS "(If the above is missing, you should properly install zeromq AND cppzmq and configure it for your platform)")

find_package(nlohmann_json 3.2.0 REQUIRED)

#fetch argparse
include(FetchContent)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)

# Sumo
add_library(sumocpp SHARED IMPORTED)
# $ENV{SUMO_HOME}/include
# $ENV{SUMO_HOME}/src

set_target_properties(sumocpp PROPERTIES
  IMPORTED_LOCATION "$ENV{SUMO_HOME}/bin/libsumocpp.so"
  INTERFACE_INCLUDE_DIRECTORIES "$ENV{SUMO_HOME}/src"
)

# Link directories and libraries

link_directories($ENV{SUMO_HOME}/bin)

target_link_libraries(ParallelSumo           cppzmq zmq sumocpp nlohmann_json::nlohmann_json argparse)
target_link_libraries(ParallelSumo-Partition cppzmq zmq sumocpp nlohmann_json::nlohmann_json argparse)

if (PSUMO_SINGLE_EXECUTABLE)
    message(STATUS "PSUMO_SINGLE_EXECUTABLE on")
    add_compile_definitions(PSUMO_SINGLE_EXECUTABLE)
else()
    message(STATUS "PSUMO_SINGLE_EXECUTABLE off")
endif()

# Add header files for IDEs that support autocompletion
target_sources(ParallelSumo PRIVATE
    ${SRC_DIR}/ParallelSim.hpp
    ${SRC_DIR}/utils.hpp
    ${SRC_DIR}/psumoTypes.hpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/globals.hpp
)
target_sources(ParallelSumo-Partition PRIVATE
    ${SRC_DIR}/NeighborPartitionHandler.hpp
    ${SRC_DIR}/PartitionEdgesStub.hpp
    ${SRC_DIR}/PartitionManager.hpp
    ${SRC_DIR}/utils.hpp
    ${SRC_DIR}/psumoTypes.hpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/partArgs.hpp
    ${SRC_DIR}/globals.hpp
)

message(STATUS "Include directories = ${INCLUDE_DIRECTORIES}")