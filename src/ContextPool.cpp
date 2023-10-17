#include "ContextPool.hpp"
#include "utils.hpp"
#include <zmq.hpp>

using namespace psumo;

// Init static vars
std::vector<zmq::context_t*> ContextPool::contexts;

zmq::context_t& ContextPool::newContext(int io_threads, int max_sockets) {
    printf("ContextPool [%d] | Adding context (%zu)\n", getPid(), contexts.size());
    auto ctx = new zmq::context_t{io_threads, max_sockets};
    contexts.push_back(ctx);
    return *ctx;
}

void ContextPool::destroyAll() {
    printf("ContextPool [%d] | Destroying all contexts...\n", getPid());
    for (auto ctx : contexts) {
        ctx->shutdown();
    }
    for (auto ctx : contexts) {
        ctx->close();
    }
    for (auto ctx : contexts) {
        delete ctx;
    }
    printf("ContextPool [%d] | Contexts destroyed\n", getPid());
}