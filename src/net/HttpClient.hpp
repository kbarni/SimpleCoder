#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// A single HTTP request. Covers the small surface the web tools need: a GET or
// POST, custom headers, an optional body, and caps so a tool can't hang or pull
// down an unbounded page.
struct HttpRequest {
  std::string method = "GET";          // "GET" or "POST"
  std::string url;
  std::vector<std::string> headers;    // each "Name: value"
  std::string body;                    // request body for POST
  long timeout_s = 15;                 // total transfer timeout
  long max_bytes = 2 * 1024 * 1024;    // cap on the stored response body
};

// The outcome of an HTTP request.
struct HttpResponse {
  long status = 0;            // HTTP status; 0 means a transport/connect failure
  std::string body;           // response body (possibly truncated to max_bytes)
  std::string content_type;   // value of the Content-Type header, if any
  std::string error;          // human-readable message when status == 0
  bool truncated = false;     // true if the body was cut off at max_bytes
};

// The injectable seam: tools take an HttpFn so a unit test can script responses
// instead of hitting the network (mirrors ChatRound / EnvLookup / the attachment
// reader). The real implementation is `http_request` below.
using HttpFn = std::function<HttpResponse(const HttpRequest&)>;

// Perform a request via libcurl. Follows redirects, enables gzip, and never
// throws — failures are reported in HttpResponse::error with status 0.
HttpResponse http_request(const HttpRequest& req);

// Percent-encode `s` for use in a URL query value (RFC 3986 unreserved set).
std::string url_encode(std::string_view s);

}  // namespace llmcli
