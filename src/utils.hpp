#pragma once

#include <map>
#include <string>
#include <zmq.hpp>
#include <filesystem>
#include <iostream>

#define EXECVP_CPP(args) \
    std::vector<char*> c_args; \
    for (const std::string& arg : args) { \
        c_args.push_back(const_cast<char*>(arg.c_str())); \
    } \
    c_args.push_back(nullptr); \
    execvp(c_args[0], c_args.data());

// Template doesn't work, for some reason
#define __printVector(v, prefix, sep, newline, stream) {\
    std::stringstream ss; ss << prefix; \
    for (const auto& element : v) { \
        stream << element; \
        if (&element != &v.back()) { \
            stream << sep; \
        } \
    } \
    if (newline) ss << endl; \
    stream << ss.str(); }

namespace psumo {
    pid_t runProcess(std::string exePath, std::vector<std::string>& args);
    pid_t waitProcess(int* status);
    pid_t getPid();
    void killProcess(pid_t pid);

    void printStackTrace();
    std::string getStackTrace();

    void printVector(
        const std::vector<std::string>&, const std::string prefix = "vector: ", 
        const std::string sep = ", ", const bool newline = true, std::ostream& stream = std::cout
    );

    std::filesystem::path getCurrentExePath();
    std::filesystem::path getCurrentExeDirectory();

    std::string getSumoPath(bool gui);
    std::filesystem::path getPartitionDataFile(std::string dataFolder, int partId);

    zmq::socket_t* makeSocket(zmq::context_t&context_, zmq::socket_type  type_);
    inline void* castPollSocket(zmq::socket_t& socket) { return socket.operator void*(); }
    zmq::message_t createMessageWithStrings(std::vector<std::string>& strings, int offset = 0, int spaceAfter = 0);
    std::vector<std::string> readStringsFromMessage(zmq::message_t& message, int offset = 0);

    inline std::string boolToString(bool x) { return x ? "true" : "false"; }

    // Some inline wrappers for zmq used in debug to check for socket connections
    // may remove later

    #ifndef NDEBUG
    static int socketCounts = 0;
    #endif

    inline void connect(zmq::socket_t& socket, const std::string addr) {
        socket.connect(addr);
        #ifndef NDEBUG
            // printStackTrace();
            socketCounts++;
            printf("\t\tConnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
        #endif
    }
    inline void bind(zmq::socket_t& socket, const std::string addr) {
        socket.bind(addr);
        #ifndef NDEBUG
            // printStackTrace();
            socketCounts++;
            printf("\t\tConnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
        #endif
    }
    inline void close(zmq::socket_t& socket) {
        socket.close();
        #ifndef NDEBUG
            // printStackTrace();
            socketCounts--;
            printf("\t\tDisconnect | Connected sockets: %d [@%d]\n", socketCounts, getPid());
        #endif
    }
}

