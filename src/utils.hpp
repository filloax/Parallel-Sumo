#pragma once

#include <string>
#include <zmq.hpp>
#include <filesystem>

#define EXECVP_CPP(args) \
    std::vector<char*> c_args; \
    for (const std::string& arg : args) { \
        c_args.push_back(const_cast<char*>(arg.c_str())); \
    } \
    c_args.push_back(nullptr); \
    execvp(c_args[0], c_args.data());

namespace psumo {
    pid_t runProcess(std::string exePath, std::vector<std::string>& args);
    pid_t waitProcess(int* status);
    pid_t getPid();

    void printStackTrace();
    std::string getStackTrace();

    std::filesystem::path getCurrentExePath();
    std::filesystem::path getCurrentExeDirectory();

    std::string getSumoPath(bool gui);
    std::filesystem::path getPartitionDataFile(std::string dataFolder, int partId);

    zmq::message_t createMessageWithStrings(std::vector<std::string>& strings, int offset = 0, int spaceAfter = 0);
    std::vector<std::string> readStringsFromMessage(zmq::message_t& message, int offset = 0);

    inline std::string boolToString(bool x) { return x ? "true" : "false"; }
}

