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

std::vector<std::string> readStringsFromMessage(zmq::message_t &message, int offset) {
    const char* data = static_cast<const char*>(message.data());
    size_t size = message.size();

    int vectorSize;
    std::memcpy(&vectorSize, data, sizeof(int));

    std::vector<std::string> result(vectorSize);
    int currentString = 0;

    size_t start = offset + sizeof(int);
    for (size_t i = start; i < size && currentString < vectorSize; ++i) {
        if (data[i] == '\0') {
            result[currentString] = string(&data[start], &data[i]);
            start = i + 1;
            currentString++;
        }
    }

    return result;
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

        auto out = readStringsFromMessage(request, 0);

        cout << "Received:" << endl;
        for (auto str : out) {
            cout << "\t" << str << endl;
        }

        // simulate work
        sleep(1);

        // send the reply to the client
        socket.send(zmq::buffer(data), zmq::send_flags::none);
    }

    socket.disconnect(addr);

    std::cout << "Done!" << std::endl;
}