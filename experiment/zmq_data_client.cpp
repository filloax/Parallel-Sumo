#include <cstring>
#include <sstream>
#include <string>
#include <zmq.hpp>
#include <iostream>

using namespace std;

#define MY_ID 1
#define TARG_ID 0

typedef int partId_t;

string getIpcSocketName(std::string directory, partId_t from, partId_t to) {
    std::stringstream out;
    out << "ipc://" << directory << "/" << from << "-" << to;
    return out.str();
}


zmq::message_t createMessageWithStrings(vector<string> &strings, int offset, int spaceAfter) {
    int totalSize = 0;
    for (auto str: strings) totalSize += str.size();

    // + vector.size(): account for null characters at end of each string
    zmq::message_t message(offset + spaceAfter + sizeof(int) + totalSize + strings.size());

    // Also write an int with the vector size
    int vectorSize = strings.size();
    std::memcpy(static_cast<int*>(message.data()) + offset, &vectorSize, sizeof(int));

    int writtenBytes = sizeof(int);
    for (int i = 0; i < strings.size(); i++) {
        std::memcpy(
            static_cast<char*>(message.data()) + offset + writtenBytes, 
            (strings[i] + '\0').data(), strings[i].size() + 1
        );
        writtenBytes += strings[i].size() + 1;
    }

    return message;
}

int main(int argc, char* argv[]) {
    auto zcontext = zmq::context_t{1};
    auto socket = zmq::socket_t{zcontext, zmq::socket_type::req};
    auto addr = getIpcSocketName("../data", MY_ID, TARG_ID);
    socket.connect(addr);

    vector<string> strings;

    for (auto request_num = 0; request_num < 10; ++request_num) 
    {
        // send the request message
        std::cout << "Sending message no. " << request_num << "..." << std::endl;

        strings.push_back(string("TestMsg") + to_string(request_num));
        auto msg = createMessageWithStrings(strings, 0, 0);
        socket.send(msg, zmq::send_flags::none);

        // wait for reply from server
        zmq::message_t reply{};
        auto result = socket.recv(reply, zmq::recv_flags::none);

        std::cout << "Received " << reply.to_string(); 
        std::cout << " (" << request_num << ")";
        std::cout << std::endl;
    }

    socket.disconnect(addr);

    std::cout << "Done!" << std::endl;
}