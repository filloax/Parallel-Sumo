cmake_minimum_required(VERSION 3.5.0)
project(ParallelSumo VERSION 0.8)

# Main config

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # require at least gcc 13
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        message(FATAL_ERROR "GCC version must be at least 13, is ${CMAKE_CXX_COMPILER_VERSION}!")
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # require at least clang 14
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR "Clang version must be at least 14, is ${CMAKE_CXX_COMPILER_VERSION}!")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif()

set(CMAKE_CXX_STANDARD 20)

option(PSUMO_SINGLE_EXECUTABLE "Create a single executable 
    using fork instead of making partitions a separate exe
    (currently doesn't work)" OFF)

if (NOT DEFINED ENV{SUMO_HOME})
    message(FATAL_ERROR 
    "SUMO_HOME environment variable must be set, both at 
    compilation and when the program runs. Normally installing SUMO should do 
    it, try rebooting if it is still not set after SUMO install, or manually set 
    it.")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files
set(SRC_DIR src)
set(LIBS_DIR libs)

set(SOURCE_FILES_COORDINATOR
    ${SRC_DIR}/ParallelSim.cpp
    ${SRC_DIR}/ContextPool.cpp
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
    ${SRC_DIR}/ContextPool.cpp
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
    ${LIBS_DIR}/argparse.hpp
)

message(STATUS "SUMO_HOME=$ENV{SUMO_HOME}")

# Include directories
include_directories(
    .
)

add_executable(ParallelSumo ${SOURCE_FILES_COORDINATOR} ${LIB_FILES})
if (NOT PSUMO_SINGLE_EXECUTABLE)
    add_executable(ParallelSumo-Partition ${SOURCE_FILES_PARTITION} ${LIB_FILES})
    add_executable(ParallelSumo-Partition-Gui ${SOURCE_FILES_PARTITION} ${LIB_FILES})
    target_compile_definitions(ParallelSumo-Partition-Gui PRIVATE HAVE_LIBSUMOGUI)
    message(STATUS "Added partition executable")
else()
    message(FATAL_ERROR "Single executable mode not supported, was left enabled for future expandability")
endif()

#find cppzmq wrapper, installed by make of cppzmq
find_package(cppzmq QUIET)
if (cppzmq_FOUND)
    message(STATUS "ZMQ CPP dir: " ${cppzmq_INCLUDE_DIR})
else()
    message(STATUS 
    "ZMQ CPP CMake config not found; some distributions do not give it as cppzmq is 
    already in the system include folders, if that isn't the case the build will error.")
endif()

find_package(nlohmann_json 3.2.0 REQUIRED)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # used only in debug
    find_package(Boost REQUIRED)
    message(STATUS "Boost dir: ${Boost_INCLUDE_DIRS}")
else()
    message(STATUS "Not debug mode, won't use boost")
endif()

# Sumo
# Libsumo should be included in /usr/include or equivalent in a standard install
# this is for using it from source, etc
if (EXISTS "$ENV{SUMO_HOME}/src")
    set(HAS_SUMOCPP true)
    set(SUMOCPP_INCLUDES "$ENV{SUMO_HOME}/src")

    find_library(SUMO_CPP_LIBRARY sumocpp PATHS "$ENV{SUMO_HOME}/bin" /usr/lib)

    add_library(sumocpp SHARED IMPORTED)

    set_target_properties(sumocpp PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES /path/client/lib
        IMPORTED_LOCATION "${SUMO_CPP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{SUMO_HOME}/src"
    )
    message(STATUS "Using sumolib from SUMO sources at $ENV{SUMO_HOME}/src")
else()
    message(STATUS "SUMO source not in SUMO_HOME, will use sumolib from standard SUMO install (this is normal, unless you want to build SUMO from source)")
endif()

# Link directories and libraries

link_directories($ENV{SUMO_HOME}/bin)

set(LIBS nlohmann_json::nlohmann_json)

if (cppzmq_FOUND)
    list(APPEND LIBS cppzmq zmq)
else()
    # Just add linker flag, assume headers are in system include folder
    list(APPEND LIBS -lzmq)
endif()

if (HAS_SUMOCPP)
    list(APPEND LIBS sumocpp)
else()
    list(APPEND LIBS -lsumocpp)
endif()

# boost only used in debug
if (Boost_FOUND)
    list(APPEND LIBS     
        ${Boost_STACKTRACE_BASIC_LIBRARY}
        ${Boost_STACKTRACE_BACKTRACE_LIBRARY}
        ${Boost_STACKTRACE_ADDR2LINE_LIBRARY}
        ${Boost_STACKTRACE_NOOP_LIBRARY}
    )
    # target_include_directories(ParallelSumo PUBLIC ${Boost_INCLUDE_DIRS})
    # target_include_directories(ParallelSumo-Partition PUBLIC ${Boost_INCLUDE_DIRS})
    # target_include_directories(ParallelSumo-Partition-Gui PUBLIC ${Boost_INCLUDE_DIRS})

    # enable stacktrace file lines
    target_compile_definitions(ParallelSumo PUBLIC BOOST_STACKTRACE_USE_ADDR2LINE _GLIBCXX_ASSERTIONS)
    target_compile_definitions(ParallelSumo-Partition PUBLIC BOOST_STACKTRACE_USE_ADDR2LINE _GLIBCXX_ASSERTIONS)
    target_compile_definitions(ParallelSumo-Partition-Gui PUBLIC BOOST_STACKTRACE_USE_ADDR2LINE _GLIBCXX_ASSERTIONS)
    target_compile_options(ParallelSumo PRIVATE "-g;-fno-inline")
    target_compile_options(ParallelSumo-Partition PRIVATE "-g;-fno-inline")
    target_compile_options(ParallelSumo-Partition-Gui PRIVATE "-g;-fno-inline")
    target_link_options(ParallelSumo PRIVATE "-no-pie")
    target_link_options(ParallelSumo-Partition PRIVATE "-no-pie")
    target_link_options(ParallelSumo-Partition-Gui PRIVATE "-no-pie")
    message(STATUS "Adding no-pie mode, security issue out of debug")

    list(APPEND LIBS dl)
endif()

message(STATUS "Used libs: ${LIBS}")

target_link_libraries(ParallelSumo LINK_PUBLIC ${LIBS})
target_link_libraries(ParallelSumo-Partition LINK_PUBLIC ${LIBS})
target_link_libraries(ParallelSumo-Partition-Gui LINK_PUBLIC ${LIBS})

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
get_target_property(TargetPar_SOURCES ParallelSumo-Partition SOURCES)
set_target_properties(ParallelSumo-Partition-Gui PROPERTIES SOURCES "${TargetPar_SOURCES}")

message(STATUS "Include directories = ${INCLUDE_DIRECTORIES}")

# Install config
# Used for CPack
install(TARGETS ParallelSumo ParallelSumo-Partition ParallelSumo-Partition-Gui)
install(DIRECTORY scripts/ DESTINATION scripts 
    PATTERN __pycache__ EXCLUDE 
    PATTERN *.ipynb EXCLUDE 
)
install(DIRECTORY assets/ DESTINATION examples 
    FILES_MATCHING PATTERN simpleNet.*
    PATTERN testMultipartRoutes.*
    PATTERN spider0.*
)
install(FILES launch.sh performance-measures-launch.sh run-with-env.sh DESTINATION .)
install(FILES DIST_README.md DESTINATION . RENAME README.md)

# Cpack config
set(CPACK_SOURCE_IGNORE_FILES "/build*/"
    "/\\\\..+"
    "/\\\\..+/"
    "/dist/"
    "/bin/"
    "/lib/"
    "/data/"
    "/img/"
    "/testResults/"
    "/experiment/"
    ${CPACK_SOURCE_IGNORE_FILES}
)
set(CPACK_PACKAGE_DIRECTORY "dist")
set(CPACK_GENERATOR "ZIP")
set(CPACK_SOURCE_GENERATOR "ZIP")
include(CPack)
