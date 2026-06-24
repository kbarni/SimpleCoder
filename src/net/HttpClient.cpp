#include "net/HttpClient.hpp"

#include <cstddef>
#include <limits>
#include <mutex>

#include <curl/curl.h>

namespace llmcli {

namespace {

// curl_global_init must run once per process before any easy handle is used.
void ensure_global_init() {
  static std::once_flag flag;
  std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Accumulates the response body, stopping once `max` bytes are stored. When the
// cap is hit it aborts the transfer (returns 0) and records the truncation.
struct WriteCtx {
  std::string body;
  std::size_t max = 0;
  bool truncated = false;
};

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb,
                     void* userdata) {
  auto* ctx = static_cast<WriteCtx*>(userdata);
  const std::size_t n = size * nmemb;
  const std::size_t room = ctx->max > ctx->body.size()
                               ? ctx->max - ctx->body.size()
                               : 0;
  if (n <= room) {
    ctx->body.append(ptr, n);
    return n;
  }
  ctx->body.append(ptr, room);
  ctx->truncated = true;
  return 0;  // signal an abort; perform() returns CURLE_WRITE_ERROR
}

}  // namespace

HttpResponse http_request(const HttpRequest& req) {
  ensure_global_init();

  HttpResponse res;
  CURL* curl = curl_easy_init();
  if (!curl) {
    res.error = "failed to initialize libcurl";
    return res;
  }

  WriteCtx ctx;
  ctx.max = req.max_bytes > 0 ? static_cast<std::size_t>(req.max_bytes)
                              : std::numeric_limits<std::size_t>::max();

  struct curl_slist* headers = nullptr;
  for (const std::string& h : req.headers) {
    headers = curl_slist_append(headers, h.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (req.method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(req.body.size()));
  }
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // enable gzip/deflate
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeout_s);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "SimpleCoder/0.1");

  const CURLcode rc = curl_easy_perform(curl);

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  char* ct = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
  if (ct) res.content_type = ct;

  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  res.body = std::move(ctx.body);
  res.truncated = ctx.truncated;
  // A write abort from hitting the byte cap is an expected stop, not a failure.
  if (rc != CURLE_OK && !(rc == CURLE_WRITE_ERROR && ctx.truncated)) {
    res.status = 0;
    res.error = curl_easy_strerror(rc);
  } else {
    res.status = status;
  }
  return res;
}

std::string url_encode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                            c == '.' || c == '~';
    if (unreserved) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

}  // namespace llmcli
