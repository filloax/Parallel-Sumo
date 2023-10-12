#include <cstring>
#include <sstream>
#include <unistd.h>
#include <zmq.hpp>
#include <iostream>

using namespace std;

#define MY_ID 0
#define TARG_ID 1

typedef int partId_t;

string getIpcSocketName(std::string directory, partId_t from, partId_t to) {
    std::stringstream out;
    out << "ipc://" << directory << "/" << from << "-" << to;
    return out.str();
}

int main(int argc, char* argv[]) {
    auto zcontext = zmq::context_t{1};
    auto socket = zmq::socket_t{zcontext, zmq::socket_type::rep};
    auto addr = getIpcSocketName("../data", TARG_ID, MY_ID);
    printf("Binding to addr %s...\n", addr.c_str());
    socket.bind(addr);

    const std::string data{"Success!"};

    for (auto request_num = 0; request_num < 10; ++request_num) 
    {
        zmq::message_t request;

        // receive a request from client
        auto result = socket.recv(request, zmq::recv_flags::none);

        double speed;

        std::memcpy(&speed, request.data(), sizeof(double));

        string veh(static_cast<char*>(request.data()) + sizeof(double), static_cast<char*>(request.data()) + request.size());

        std::cout << "Received " << speed <<  " " << veh << " (size " << request.size() << ")" << std::endl;

        // simulate work
        sleep(1);

        // send the reply to the client
        socket.send(zmq::buffer(data), zmq::send_flags::none);
    }

    socket.disconnect(addr);

    std::cout << "Done!" << std::endl;
}