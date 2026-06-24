#include "agent/Tool.hpp"

#include <algorithm>

#include "net/Html.hpp"

namespace llmcli {

using nlohmann::json;

namespace {
constexpr int kDefaultMaxChars = 20000;
constexpr int kMaxMaxChars = 200000;
}  // namespace

FetchUrlTool::FetchUrlTool(HttpFn http)
    : http_(http ? std::move(http) : HttpFn(http_request)) {}

json FetchUrlTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Fetch a web page over HTTP(S) and return its readable text content "
         "(HTML markup removed). Use web_search first to find URLs."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"url",
             {{"type", "string"},
              {"description", "Absolute http:// or https:// URL to fetch."}}},
            {"max_chars",
             {{"type", "integer"},
              {"description",
               "Maximum characters of text to return (default 20000)."}}}}},
          {"required", json::array({"url"})}}}}}};
}

ToolResult FetchUrlTool::execute(const json& args) const {
  const std::string url = args.value("url", "");
  if (url.empty()) return {false, "error: 'url' argument is required"};
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    return {false, "error: 'url' must be an absolute http:// or https:// URL"};
  }

  int max_chars = args.value("max_chars", kDefaultMaxChars);
  if (max_chars <= 0) max_chars = kDefaultMaxChars;
  max_chars = std::min(max_chars, kMaxMaxChars);

  HttpRequest req;
  req.url = url;
  HttpResponse resp = http_(req);

  if (resp.status == 0) {
    return {false, "error: request failed: " +
                       (resp.error.empty() ? "unknown error" : resp.error)};
  }
  if (resp.status >= 400) {
    return {false, "error: HTTP " + std::to_string(resp.status) +
                       " fetching " + url};
  }

  std::string text = html_to_text(resp.body, resp.content_type);
  if (static_cast<int>(text.size()) > max_chars) {
    text.resize(max_chars);
    text += "\n… [truncated]";
  } else if (resp.truncated) {
    text += "\n… [truncated]";
  }
  if (text.empty()) return {true, "(no readable text on page)"};
  return {true, text};
}

}  // namespace llmcli
