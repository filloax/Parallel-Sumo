#pragma once

#include <string>
#include <zmq.hpp>

#define EXECVP_CPP(args) \
    do { \
        std::vector<char*> c_args; \
        for (const std::string& arg : args) { \
            c_args.push_back(const_cast<char*>(arg.c_str())); \
        } \
        c_args.push_back(nullptr); \
        execvp(c_args[0], c_args.data()); \
    } while (0)

void printStackTrace();
std::string getStackTrace();

zmq::message_t createMessageWithStrings(std::vector<std::string>& strings, int offset = 0, int spaceAfter = 0);
std::vector<std::string> readStringsFromMessage(zmq::message_t& message, int offset = 0);