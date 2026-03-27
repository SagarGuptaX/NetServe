#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "net/server.h"

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------
static void print_usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog
        << " <port> [thread_count]"
        << " --cache-audit <path>"
        << " --traces-dir <path>\n\n"
        << "Arguments:\n"
        << "  port           TCP port to listen on (1–65535)\n"
        << "  thread_count   Worker threads (optional, default: hardware_concurrency)\n"
        << "  --cache-audit  Path to the cache_audit binary\n"
        << "  --traces-dir   Path to the directory containing trace .txt files\n\n"
        << "Example:\n"
        << "  " << prog
        << " 8080 --cache-audit ../CacheAudit/cache_audit"
        << " --traces-dir ../CacheAudit/traces/synthetic\n"
        << "  " << prog
        << " 8080 4 --cache-audit ./cache_audit --traces-dir ./traces/synthetic\n";
}

// ---------------------------------------------------------------------------
// Argument parsers
// ---------------------------------------------------------------------------
static int parse_port(const char* str) {
    std::size_t pos = 0;
    int port = std::stoi(str, &pos);
    if (str[pos] != '\0') throw std::invalid_argument("trailing characters");
    if (port <= 0 || port > 65535) throw std::out_of_range("out of range");
    return port;
}

static std::size_t parse_threads(const char* str) {
    std::size_t pos = 0;
    int n = std::stoi(str, &pos);
    if (str[pos] != '\0') throw std::invalid_argument("trailing characters");
    if (n <= 0) throw std::out_of_range("must be positive");
    return static_cast<std::size_t>(n);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // --- Positional: port ---
    int port = 0;
    try {
        port = parse_port(argv[1]);
    } catch (const std::exception&) {
        std::cerr << "Error: invalid port '" << argv[1] << "'\n";
        return EXIT_FAILURE;
    }

    // --- Positional: optional thread count ---
    // If argv[2] doesn't start with '-', treat it as thread_count.
    std::size_t threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 4;

    int named_start = 2; // index where named args begin
    if (argc >= 3 && argv[2][0] != '-') {
        try {
            threads = parse_threads(argv[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: invalid thread count '" << argv[2] << "'\n";
            return EXIT_FAILURE;
        }
        named_start = 3;
    }

    // --- Named args: --cache-audit and --traces-dir ---
    std::string cache_audit_bin;
    std::string traces_dir;

    for (int i = named_start; i < argc - 1; ++i) {
        std::string flag = argv[i];
        if (flag == "--cache-audit") {
            cache_audit_bin = argv[++i];
        } else if (flag == "--traces-dir") {
            traces_dir = argv[++i];
        } else {
            std::cerr << "Error: unknown argument '" << flag << "'\n";
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // --- Validate required named args are present ---
    if (cache_audit_bin.empty()) {
        std::cerr << "Error: --cache-audit is required\n";
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (traces_dir.empty()) {
        std::cerr << "Error: --traces-dir is required\n";
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // --- Validate paths exist BEFORE binding the socket ---
    // Fail loudly now rather than silently at request time.
    if (!std::filesystem::exists(cache_audit_bin)) {
        std::cerr << "Error: cache_audit binary not found at '"
                  << cache_audit_bin << "'\n";
        return EXIT_FAILURE;
    }
    if (!std::filesystem::is_regular_file(cache_audit_bin)) {
        std::cerr << "Error: --cache-audit path exists but is not a file: '"
                  << cache_audit_bin << "'\n";
        return EXIT_FAILURE;
    }
    if (!std::filesystem::exists(traces_dir)) {
        std::cerr << "Error: traces directory not found at '"
                  << traces_dir << "'\n";
        return EXIT_FAILURE;
    }
    if (!std::filesystem::is_directory(traces_dir)) {
        std::cerr << "Error: --traces-dir path exists but is not a directory: '"
                  << traces_dir << "'\n";
        return EXIT_FAILURE;
    }

    // --- Print confirmed config before starting ---
    std::cout << "cache_audit : " << cache_audit_bin << "\n";
    std::cout << "traces dir  : " << traces_dir      << "\n";
    std::cout << "threads     : " << threads          << "\n";

    // --- Start server ---
    try {
        net::HttpServer server(port, threads, cache_audit_bin, traces_dir);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
