#pragma once

#include "http/http_message.h"
#include <string>

namespace http {

// Parses a raw HTTP/1.1 request into an HttpRequest.
// Splits path from query string and parses query params automatically.
HttpRequest parse_request(const std::string& raw);

// Serialises an HttpResponse into a valid HTTP/1.1 wire-format string.
std::string serialize_response(const HttpResponse& response);

} // namespace http
