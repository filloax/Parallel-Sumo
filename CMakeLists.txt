cmake_minimum_required(VERSION 3.0.0)
project(ParallelSumo)

set(CMAKE_CXX_STANDARD 17)

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

set(SOURCE_FILES
    ${SRC_DIR}/NeighborPartitionHandler.cpp
    ${SRC_DIR}/ParallelSim.cpp
    ${SRC_DIR}/PartitionEdgesStub.cpp
    ${SRC_DIR}/PartitionManager.cpp
    ${SRC_DIR}/args.hpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/utils.cpp
    ${SRC_DIR}/messagingShared.cpp
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

add_executable(ParallelSumo ${SOURCE_FILES} ${LIB_FILES})

# ## load in pkg-config support
# find_package(PkgConfig)
# ## use pkg-config to get hints for 0mq locations
# pkg_check_modules(PC_ZeroMQ QUIET zmq)

#find cppzmq wrapper, installed by make of cppzmq
find_package(cppzmq)
target_link_libraries(ParallelSumo cppzmq)
message(STATUS "ZMQ CPP dir: " ${cppzmq_INCLUDE_DIR})
message(STATUS "(If the above is missing, you should properly install zeromq AND cppzmq and configure it for your platform)")

# ## use the hint from above to find where 'zmq.hpp' is located
# find_path(ZeroMQ_INCLUDE_DIR
#         NAMES zmq.hpp
#         PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
#         )
# message(STATUS "ZeroMQ include dir: " ${ZeroMQ_INCLUDE_DIR})

# ## use the hint from above to find the location of libzmq
# find_library(ZeroMQ_LIBRARY
#         NAMES zmq
#         PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
#         )
# message(STATUS "ZeroMQ library: " ${ZeroMQ_LIBRARY})
# message(STATUS "(If the above is missing, you should properly install zeromq and configure it for your platform)")

# Link directories and libraries
target_link_libraries(ParallelSumo sumocpp zmq)
link_directories(${SUMO_HOME}/bin)

# Add header files for IDEs that support autocompletion
target_sources(ParallelSumo PRIVATE
    ${SRC_DIR}/NeighborPartitionHandler.hpp
    ${SRC_DIR}/ParallelSim.hpp
    ${SRC_DIR}/PartitionEdgesStub.hpp
    ${SRC_DIR}/PartitionManager.hpp
    ${SRC_DIR}/utils.hpp
)

message(STATUS "Include directories = ${INCLUDE_DIRECTORIES}")