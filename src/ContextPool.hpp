/*
ContextPool.hpp

Manage N zeromq contexts, for the purpose of parallel I/O with different peers
in different threads, and destroy them at the end of the process.

Author: Filippo Lenzi
*/

#pragma once

#include <vector>
#include <zmq.hpp>

namespace psumo {

/**
Manage N zeromq contexts, for the purpose of parallel I/O with different peers
in different threads, and destroy them at the end of the process.
(Reason being, destroying context seemingly leading to bugs, which makes 
debugging other bugs in case of this harder).

Do NOT delete contexts handled by this out of the class.
*/
class ContextPool {
private:
    static std::vector<zmq::context_t*> contexts;

public:
    static zmq::context_t& newContext(int io_threads = 1, int max_sockets = ZMQ_MAX_SOCKETS_DFLT);
    static void destroyAll();
};

}