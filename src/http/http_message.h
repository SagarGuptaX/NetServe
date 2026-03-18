#pragma once

#include <string>
#include <unordered_map>

namespace http {

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
};

struct HttpResponse {
    int status_code{200};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

} // namespace http
