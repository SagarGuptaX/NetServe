#include "net/server.h"

#include <stdexcept>

namespace net {

HttpServer::HttpServer(int port) : port_(port) {}

void HttpServer::start() {
    throw std::logic_error("Not implemented");
}

void HttpServer::stop() {
    throw std::logic_error("Not implemented");
}

void HttpServer::setup_socket() {
    throw std::logic_error("Not implemented");
}

void HttpServer::event_loop() {
    throw std::logic_error("Not implemented");
}

} // namespace net
