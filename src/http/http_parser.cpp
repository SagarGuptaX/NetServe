#include "http/http_parser.h"

#include <sstream>
#include <unordered_map>

namespace http {

// ─── Internal helpers ─────────────────────────────────────────────────────────

static std::unordered_map<std::string, std::string>
parse_query_string(const std::string& qs) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream stream(qs);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos)
            params[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
    return params;
}

// ─── Public ───────────────────────────────────────────────────────────────────

HttpRequest parse_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    // Request line: METHOD path HTTP/x.x
    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream rl(line);
        std::string full_path, version;
        rl >> req.method >> full_path >> version;

        auto q = full_path.find('?');
        if (q != std::string::npos) {
            req.path         = full_path.substr(0, q);
            req.query_string = full_path.substr(q + 1);
            req.query_params = parse_query_string(req.query_string);
        } else {
            req.path = full_path;
        }
    }

    // Headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // blank line = end of headers

        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            auto first = val.find_first_not_of(" \t");
            if (first != std::string::npos) val = val.substr(first);
            req.headers[std::move(key)] = std::move(val);
        }
    }

    return req;
}

std::string serialize_response(const HttpResponse& response) {
    static const std::unordered_map<int, std::string> STATUS_TEXT = {
        {200, "OK"},
        {400, "Bad Request"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
    };

    auto it = STATUS_TEXT.find(response.status_code);
    const std::string& reason =
        (it != STATUS_TEXT.end()) ? it->second : "Unknown";

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status_code << " " << reason << "\r\n";
    for (const auto& [k, v] : response.headers)
        out << k << ": " << v << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n" << response.body;
    return out.str();
}

} // namespace http
