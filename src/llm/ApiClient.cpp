#include "llm/ApiClient.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include <curl/curl.h>

#include "llm/ChatRequest.hpp"

namespace llmcli {

namespace {

// curl_global_init must run once per process before any easy handle is used.
void ensure_global_init() {
  static std::once_flag flag;
  std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Join the base URL and path, tolerating a trailing slash on the base.
std::string join_url(std::string base, std::string_view path) {
  if (!base.empty() && base.back() == '/') base.pop_back();
  return base + std::string(path);
}

// The server root, i.e. base_url with a trailing "/v1" path segment removed, so
// non-versioned endpoints like llama.cpp's /props can be reached.
std::string server_root(std::string base) {
  if (!base.empty() && base.back() == '/') base.pop_back();
  constexpr std::string_view v1 = "/v1";
  if (base.size() >= v1.size() &&
      base.compare(base.size() - v1.size(), v1.size(), v1) == 0) {
    base.erase(base.size() - v1.size());
  }
  return base;
}

// Append received bytes to a std::string (libcurl write callback for GETs).
std::size_t append_cb(char* ptr, std::size_t size, std::size_t nmemb,
                      void* userdata) {
  const std::size_t n = size * nmemb;
  static_cast<std::string*>(userdata)->append(ptr, n);
  return n;
}

// A simple blocking GET. Returns the body on a 2xx response, else nullopt.
std::optional<std::string> http_get(const std::string& url,
                                    const std::string& api_key) {
  CURL* curl = curl_easy_init();
  if (!curl) return std::nullopt;

  std::string body;
  struct curl_slist* headers = nullptr;
  std::string auth;
  if (!api_key.empty()) {
    auth = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "SimpleCoder");

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK || status < 200 || status >= 300) return std::nullopt;
  return body;
}

// State shared with the libcurl write callback.
struct WriteCtx {
  CURL* handle = nullptr;
  StreamParser* parser = nullptr;
  std::string error_body;   // accumulates the body on an error status
  long status = 0;
  bool status_known = false;
};

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb,
                     void* userdata) {
  auto* ctx = static_cast<WriteCtx*>(userdata);
  const std::size_t n = size * nmemb;

  if (!ctx->status_known) {
    curl_easy_getinfo(ctx->handle, CURLINFO_RESPONSE_CODE, &ctx->status);
    if (ctx->status != 0) ctx->status_known = true;
  }

  const std::string_view chunk(ptr, n);
  if (ctx->status >= 400) {
    ctx->error_body.append(chunk);  // not SSE; keep for the error message
  } else {
    ctx->parser->feed(chunk);
  }
  return n;
}

}  // namespace

ApiClient::ApiClient(Config cfg) : cfg_(std::move(cfg)) { ensure_global_init(); }

namespace {
// libcurl progress callback: abort the transfer (non-zero return) once the
// shared cancel flag is set. Captureless so it converts to a C function pointer.
int xfer_cancel_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t,
                   curl_off_t) {
  const auto* cancel = static_cast<const std::atomic<bool>*>(clientp);
  return (cancel && cancel->load(std::memory_order_relaxed)) ? 1 : 0;
}
}  // namespace

ApiResult ApiClient::chat(const std::vector<Message>& messages,
                          const nlohmann::json& tools,
                          const StreamParser::ContentCallback& on_content,
                          const StreamParser::ContentCallback& on_reasoning,
                          const std::atomic<bool>* cancel) {
  ApiResult result;

  CURL* curl = curl_easy_init();
  if (!curl) {
    result.error = "failed to initialize libcurl";
    return result;
  }

  const std::string url = join_url(cfg_.base_url, "/chat/completions");
  const std::string body =
      build_chat_request(cfg_, messages, tools, /*stream=*/true).dump();

  // Time generation from the first streamed token (content or reasoning) to the
  // end of the stream, so the UI can show tokens/second. Prompt-processing time
  // before the first token is deliberately excluded.
  std::optional<std::chrono::steady_clock::time_point> first_token;
  auto mark_first = [&first_token] {
    if (!first_token) first_token = std::chrono::steady_clock::now();
  };
  StreamParser parser(
      [&](std::string_view d) {
        mark_first();
        if (on_content) on_content(d);
      },
      [&](std::string_view d) {
        mark_first();
        if (on_reasoning) on_reasoning(d);
      });
  WriteCtx ctx;
  ctx.handle = curl;
  ctx.parser = &parser;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: text/event-stream");
  std::string auth;
  if (!cfg_.api_key.empty()) {
    auth = "Authorization: Bearer " + cfg_.api_key;
    headers = curl_slist_append(headers, auth.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "SimpleCoder/0.1");
  if (cancel) {
    // Poll the cancel flag via the progress callback to abort mid-stream.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_cancel_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<std::atomic<bool>*>(cancel));
  }

  const CURLcode rc = curl_easy_perform(curl);

  if (rc == CURLE_ABORTED_BY_CALLBACK) {
    result.canceled = true;
    result.error = "request canceled";
    // status stays 0 -> treated as a failed round; history is rolled back
  } else if (rc != CURLE_OK) {
    result.error = curl_easy_strerror(rc);
    // status stays 0 -> signals a transport/connection failure to callers
  } else {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    result.http_status = status;
    if (status >= 200 && status < 300) {
      result.ok = true;
      result.content = parser.content();
      result.reasoning = parser.reasoning();
      result.tool_calls = parser.tool_calls();
      result.finish_reason = parser.finish_reason();
      result.prompt_tokens = parser.prompt_tokens();
      result.completion_tokens = parser.completion_tokens();
      result.total_tokens = parser.total_tokens();
      if (first_token) {
        result.gen_seconds = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - *first_token)
                                 .count();
      }
    } else {
      std::string detail =
          ctx.error_body.empty() ? "no response body" : ctx.error_body;
      result.error = "HTTP " + std::to_string(status) + ": " + detail;
    }
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return result;
}

std::optional<int> ApiClient::probe_context_size() {
  using nlohmann::json;

  // llama.cpp: GET /props -> n_ctx (top level on newer builds, or nested under
  // default_generation_settings on older ones).
  if (auto body = http_get(server_root(cfg_.base_url) + "/props", cfg_.api_key)) {
    json j = json::parse(*body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_discarded()) {
      if (auto n = j.find("n_ctx"); n != j.end() && n->is_number_integer()) {
        if (n->get<int>() > 0) return n->get<int>();
      }
      if (auto s = j.find("default_generation_settings");
          s != j.end() && s->is_object()) {
        if (auto n = s->find("n_ctx");
            n != s->end() && n->is_number_integer() && n->get<int>() > 0) {
          return n->get<int>();
        }
      }
    }
  }

  // vLLM (and compatible): GET /v1/models -> data[0].max_model_len.
  if (auto body = http_get(join_url(cfg_.base_url, "/models"), cfg_.api_key)) {
    json j = json::parse(*body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_discarded()) {
      if (auto d = j.find("data"); d != j.end() && d->is_array()) {
        for (const json& m : *d) {
          if (auto n = m.find("max_model_len");
              n != m.end() && n->is_number_integer() && n->get<int>() > 0) {
            return n->get<int>();
          }
        }
      }
    }
  }

  return std::nullopt;
}

std::vector<std::string> parse_model_ids(const nlohmann::json& j) {
  std::vector<std::string> ids;
  if (auto d = j.find("data"); d != j.end() && d->is_array()) {
    for (const nlohmann::json& m : *d) {
      if (auto id = m.find("id"); id != m.end() && id->is_string())
        ids.push_back(id->get<std::string>());
    }
  }
  return ids;
}

std::vector<std::string> ApiClient::list_models() {
  auto body = http_get(join_url(cfg_.base_url, "/models"), cfg_.api_key);
  if (!body) return {};
  nlohmann::json j = nlohmann::json::parse(*body, nullptr, /*exceptions=*/false);
  if (j.is_discarded()) return {};
  return parse_model_ids(j);
}

}  // namespace llmcli
