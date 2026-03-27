#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

#include "concurrency/thread_pool.h"
#include "http/http_message.h"

namespace net {

class HttpServer {
public:
    // cache_audit_bin: path to the cache_audit binary (validated by main before here).
    // traces_dir:      path to directory containing trace .txt files.
    explicit HttpServer(int port,
                        std::size_t thread_count,
                        std::string cache_audit_bin,
                        std::string traces_dir);

    HttpServer(const HttpServer&)            = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Blocks until stop() is called or a fatal error occurs.
    void start();
    void stop();

private:
    int         port_;
    int         listen_fd_{-1};
    std::string cache_audit_bin_; // path to cache_audit binary
    std::string traces_dir_;      // path to traces directory

    std::atomic<bool>                        running_{false};
    std::unique_ptr<concurrency::ThreadPool> thread_pool_;

    void setup_socket();
    void event_loop();
    void handle_client(int client_fd);

    http::HttpResponse route(const http::HttpRequest& req);
    http::HttpResponse handle_health();
    http::HttpResponse handle_cache_sim(const http::HttpRequest& req);
};

} // namespace net
