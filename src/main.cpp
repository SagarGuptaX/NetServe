#include <iostream>
#include <stdexcept>
#include <string>

#include "net/server.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return EXIT_FAILURE;
    }

    int port = 0;
    try {
        std::size_t pos = 0;
        port = std::stoi(argv[1], &pos);
        if (argv[1][pos] != '\0') {
            throw std::invalid_argument("trailing characters");
        }
    } catch (const std::exception&) {
        std::cerr << "Error: invalid port number '" << argv[1] << "'\n";
        return EXIT_FAILURE;
    }
    if (port <= 0 || port > 65535) {
        std::cerr << "Error: port must be in the range 1–65535\n";
        return EXIT_FAILURE;
    }

    try {
        net::HttpServer server(port);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
