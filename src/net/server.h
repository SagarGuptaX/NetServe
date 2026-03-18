#pragma once

namespace net {

class HttpServer {
public:
    explicit HttpServer(int port);

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();

private:
    int port_;

    void setup_socket();
    void event_loop();
};

} // namespace net
