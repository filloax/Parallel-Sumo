#include <cstring>
#include <sstream>
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


#define copy_num(type, var, message, offset) std::memcpy(\
    static_cast<type*>(message.data()) + offset,\
    &var + offset,\
    sizeof(type)\
)

void setVehicleSpeed(zmq::socket_t& socket, const string& vehId, double speed) {
    int idSize = vehId.size();
    int msgLength = sizeof(double) + idSize + 1;
    zmq::message_t message(msgLength);

    copy_num(double, speed, message, 0);
    std::memcpy(
        static_cast<char*>(message.data()) + sizeof(double),
        vehId.data(), 
        idSize + 1
    );

    socket.send(message, zmq::send_flags::none);
}

int main(int argc, char* argv[]) {
    auto zcontext = zmq::context_t{1};
    auto socket = zmq::socket_t{zcontext, zmq::socket_type::req};
    auto addr = getIpcSocketName("../data", MY_ID, TARG_ID);
    socket.connect(addr);

    for (auto request_num = 0; request_num < 10; ++request_num) 
    {
        // send the request message
        std::cout << "Sending message no. " << request_num << "..." << std::endl;
        setVehicleSpeed(socket, "veh" + to_string(request_num), request_num * 2);
        
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