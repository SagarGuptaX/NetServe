#include "net/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "http/http_parser.h"

namespace net {

static constexpr int         BACKLOG  = 128;
static constexpr std::size_t RECV_BUF = 4096;

// ─── Response factories ───────────────────────────────────────────────────────

static http::HttpResponse make_text(int code, std::string body) {
    http::HttpResponse r;
    r.status_code             = code;
    r.body                    = std::move(body) + "\n";
    r.headers["Content-Type"] = "text/plain";
    return r;
}

static http::HttpResponse make_json(int code, std::string body) {
    http::HttpResponse r;
    r.status_code             = code;
    r.body                    = std::move(body) + "\n";
    r.headers["Content-Type"] = "application/json";
    return r;
}

// ─── Output parsing helpers ───────────────────────────────────────────────────

// Finds the value that appears after `label` on the same line of `text`.
// Returns empty string if the label is not found.
// Example: find_field("Hit Rate:   95.00%\n", "Hit Rate:") → "95.00%"
static std::string find_field(const std::string& text, const std::string& label) {
    auto pos = text.find(label);
    if (pos == std::string::npos) return "";
    pos += label.size();
    // Skip leading whitespace after the colon.
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    // Read up to end of line.
    auto end = text.find_first_of("\r\n", pos);
    return (end == std::string::npos) ? text.substr(pos) : text.substr(pos, end - pos);
}

// ─── Construction ─────────────────────────────────────────────────────────────

HttpServer::HttpServer(int port,
                       std::size_t thread_count,
                       std::string cache_audit_bin,
                       std::string traces_dir)
    : port_(port),
      cache_audit_bin_(std::move(cache_audit_bin)),
      traces_dir_(std::move(traces_dir)),
      thread_pool_(std::make_unique<concurrency::ThreadPool>(thread_count)) {}

// ─── Public API ───────────────────────────────────────────────────────────────

void HttpServer::start() {
    setup_socket();
    running_ = true;
    std::cout << "NetServe listening on :" << port_
              << "  (thread pool ready)\n";
    event_loop();
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ != -1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ─── Socket setup ─────────────────────────────────────────────────────────────

void HttpServer::setup_socket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error("socket() failed");

    int opt = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("bind() failed — is port " +
                                 std::to_string(port_) + " already in use?");
    }

    if (::listen(listen_fd_, BACKLOG) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("listen() failed");
    }
}

// ─── Event loop ───────────────────────────────────────────────────────────────

void HttpServer::event_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        int client_fd = ::accept(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len);

        if (client_fd < 0) {
            if (!running_) break;
            std::cerr << "accept() error — continuing\n";
            continue;
        }

        // Hand off to the thread pool immediately so accept() can loop again.
        thread_pool_->enqueue_task([this, client_fd]() {
            handle_client(client_fd);
        });
    }
}

// ─── Per-connection handler ───────────────────────────────────────────────────

void HttpServer::handle_client(int client_fd) {
    std::string raw;
    std::array<char, RECV_BUF> buf{};
    ssize_t n;

    while ((n = ::recv(client_fd, buf.data(), buf.size(), 0)) > 0) {
        raw.append(buf.data(), static_cast<std::size_t>(n));
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }

    http::HttpResponse res;
    if (raw.empty()) {
        res = make_text(400, "Bad Request");
    } else {
        http::HttpRequest req = http::parse_request(raw);
        res = route(req);
    }

    std::string out = http::serialize_response(res);
    ::send(client_fd, out.c_str(), out.size(), 0);
    ::close(client_fd);
}

// ─── Routing ──────────────────────────────────────────────────────────────────

http::HttpResponse HttpServer::route(const http::HttpRequest& req) {
    if (req.method != "GET")
        return make_text(405, "Method Not Allowed");

    if (req.path == "/health")
        return handle_health();

    if (req.path == "/run-cache-sim")
        return handle_cache_sim(req);

    return make_text(404, "Not Found");
}

http::HttpResponse HttpServer::handle_health() {
    return make_text(200, "OK");
}

// ─── /run-cache-sim — orchestrates cache_audit as a subprocess ───────────────
//
// Architecture: NetServe does NOT implement any cache logic itself.
// It validates the request parameters, constructs a shell command,
// runs cache_audit via popen(), captures its stdout, parses the
// result lines, and returns a JSON response.
//
// This is the correct design:
//   Client → NetServe → cache_audit → result → Client
//
http::HttpResponse HttpServer::handle_cache_sim(const http::HttpRequest& req) {

    // ── Valid values (whitelisted to prevent command injection) ───────────────

    static const std::unordered_set<std::string> VALID_POLICIES = {
        "fifo", "lru", "lfu", "arc", "belady"
    };

    // Maps the user-facing trace name to the filename inside traces_dir_.
    // Only these four names are accepted — no arbitrary paths from the user.
    static const std::unordered_map<std::string, std::string> TRACE_FILES = {
        {"loop",     "loop.txt"},
        {"scan",     "scan.txt"},
        {"skewed",   "skewed.txt"},
        {"hot_cold", "hot_cold.txt"},
    };

    // ── Validate `policy` (required) ─────────────────────────────────────────

    auto pol_it = req.query_params.find("policy");
    if (pol_it == req.query_params.end() || pol_it->second.empty())
        return make_json(400,
            R"({"error":"missing required query param: policy"})");

    const std::string policy = pol_it->second;
    if (VALID_POLICIES.find(policy) == VALID_POLICIES.end())
        return make_json(400,
            R"({"error":"invalid policy — must be one of: fifo, lru, lfu, arc, belady"})");

    // ── Validate `trace` (optional, default: loop) ────────────────────────────

    std::string trace = "loop";
    auto tr_it = req.query_params.find("trace");
    if (tr_it != req.query_params.end() && !tr_it->second.empty())
        trace = tr_it->second;

    auto path_it = TRACE_FILES.find(trace);
    if (path_it == TRACE_FILES.end())
        return make_json(400,
            R"({"error":"invalid trace — must be one of: loop, scan, skewed, hot_cold"})");

    // Build the full path: traces_dir_ / filename
    // e.g. "../CacheAudit/traces/synthetic" + "/" + "loop.txt"
    const std::string trace_path = traces_dir_ + "/" + path_it->second;

    // ── Validate `cache_size` (optional, default: 32) ─────────────────────────

    int cache_size = 32;
    auto sz_it = req.query_params.find("cache_size");
    if (sz_it != req.query_params.end() && !sz_it->second.empty()) {
        try {
            std::size_t pos = 0;
            cache_size = std::stoi(sz_it->second, &pos);
            if (sz_it->second[pos] != '\0' || cache_size <= 0)
                throw std::invalid_argument("bad");
        } catch (...) {
            return make_json(400,
                R"({"error":"invalid cache_size — must be a positive integer"})");
        }
    }

    // ── Build command ─────────────────────────────────────────────────────────
    //
    // All three inputs (policy, trace_path, cache_size) are validated against
    // whitelists or parsed as integers, so there is no risk of injection here.
    //
    // Example: ./cache_audit traces/synthetic/loop.txt lru 32
    //
    // stderr is redirected to /dev/null so only the structured stdout output
    // reaches us. A non-zero exit code signals something went wrong.

    std::string cmd =
        cache_audit_bin_ + " " + trace_path + " " + policy + " " +
        std::to_string(cache_size) + " 2>/dev/null";

    // ── Run cache_audit via popen() ───────────────────────────────────────────

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return make_json(500,
            R"({"error":"failed to launch cache_audit — is the binary present in the working directory?"})");

    // Read all stdout into a string.
    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        output += buf;

    int exit_status = pclose(pipe);

    if (exit_status != 0)
        return make_json(500,
            R"({"error":"cache_audit exited with an error — check that the trace file exists"})");

    // ── Parse cache_audit output ──────────────────────────────────────────────
    //
    // Expected stdout format from cache_audit:
    //   Hit Rate:   95.00%
    //   Hits:       570
    //   Misses:     30
    //   Runtime:    0 ms

    std::string hit_rate_raw = find_field(output, "Hit Rate:");
    std::string hits_raw     = find_field(output, "Hits:");
    std::string misses_raw   = find_field(output, "Misses:");
    std::string runtime_raw  = find_field(output, "Runtime:");

    if (hit_rate_raw.empty() || hits_raw.empty() || misses_raw.empty()) {
        // Parsing failed — return raw output so the caller can debug.
        std::ostringstream err;
        err << "{\"error\":\"could not parse cache_audit output\","
            << "\"raw_output\":\"" << output << "\"}";
        return make_json(500, err.str());
    }

    // Strip trailing "%" from hit rate, then convert "95.00" → 0.9500.
    if (!hit_rate_raw.empty() && hit_rate_raw.back() == '%')
        hit_rate_raw.pop_back();
    double hit_rate = 0.0;
    try { hit_rate = std::stod(hit_rate_raw) / 100.0; } catch (...) {}

    // Strip " ms" from runtime.
    auto ms_pos = runtime_raw.find(" ms");
    if (ms_pos != std::string::npos) runtime_raw = runtime_raw.substr(0, ms_pos);

    // ── Build JSON response ───────────────────────────────────────────────────

    std::ostringstream hr_str;
    hr_str.precision(4);
    hr_str << std::fixed << hit_rate;

    std::ostringstream json;
    json << "{"
         << "\"status\":\"ok\","
         << "\"policy\":\"" << policy << "\","
         << "\"cache_size\":"  << cache_size     << ","
         << "\"trace\":\""     << trace          << "\","
         << "\"hit_rate\":"    << hr_str.str()   << ","
         << "\"hits\":"        << hits_raw       << ","
         << "\"misses\":"      << misses_raw     << ","
         << "\"runtime_ms\":"  << runtime_raw
         << "}";

    return make_json(200, json.str());
}

} // namespace net
