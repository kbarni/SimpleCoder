#include "agent/Tool.hpp"

#include <algorithm>

namespace llmcli {

using nlohmann::json;

namespace {

constexpr int kDefaultMaxResults = 5;
constexpr int kMaxMaxResults = 10;
constexpr std::size_t kMaxSnippet = 300;

// Join a base URL and a path, tolerating a trailing slash on the base.
std::string join_url(std::string base, std::string_view path) {
  if (!base.empty() && base.back() == '/') base.pop_back();
  return base + std::string(path);
}

std::string truncate(std::string s, std::size_t n) {
  if (s.size() > n) {
    s.resize(n);
    s += "…";
  }
  return s;
}

}  // namespace

WebSearchTool::WebSearchTool(WebToolsConfig cfg, HttpFn http)
    : cfg_(std::move(cfg)),
      http_(http ? std::move(http) : HttpFn(http_request)) {}

json WebSearchTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Search the web and return a ranked list of results (title, URL, "
         "snippet). Follow up with fetch_url to read a result."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"query",
             {{"type", "string"},
              {"description", "The search query."}}},
            {"max_results",
             {{"type", "integer"},
              {"description", "Maximum results to return (default 5)."}}}}},
          {"required", json::array({"query"})}}}}}};
}

std::vector<SearchResult> parse_search_results(const std::string& backend,
                                               const json& j) {
  std::vector<SearchResult> out;
  auto push = [&out](const json& r, const char* tk, const char* uk,
                     const char* sk) {
    SearchResult s;
    s.title = r.value(tk, "");
    s.url = r.value(uk, "");
    s.snippet = r.value(sk, "");
    if (!s.url.empty()) out.push_back(std::move(s));
  };

  if (backend == "brave") {
    if (auto w = j.find("web"); w != j.end() && w->is_object()) {
      if (auto rs = w->find("results"); rs != w->end() && rs->is_array()) {
        for (const json& r : *rs) push(r, "title", "url", "description");
      }
    }
  } else {
    // searxng and tavily both expose results:[{title,url,content}].
    if (auto rs = j.find("results"); rs != j.end() && rs->is_array()) {
      for (const json& r : *rs) push(r, "title", "url", "content");
    }
  }
  return out;
}

ToolResult WebSearchTool::execute(const json& args) const {
  const std::string query = args.value("query", "");
  if (query.empty()) return {false, "error: 'query' argument is required"};
  if (cfg_.search_url.empty()) {
    return {false,
            "error: web search is not configured. Set 'search_url' (and a "
            "'search_backend' / 'search_api_key' if needed) in config.conf."};
  }

  int max_results = args.value("max_results", kDefaultMaxResults);
  if (max_results <= 0) max_results = kDefaultMaxResults;
  max_results = std::min(max_results, kMaxMaxResults);

  HttpRequest req;
  if (cfg_.search_backend == "tavily") {
    req.method = "POST";
    req.url = cfg_.search_url;
    req.headers.push_back("Content-Type: application/json");
    json payload = {{"api_key", cfg_.search_api_key},
                    {"query", query},
                    {"max_results", max_results}};
    req.body = payload.dump();
  } else if (cfg_.search_backend == "brave") {
    req.url = cfg_.search_url + "?q=" + url_encode(query);
    req.headers.push_back("Accept: application/json");
    if (!cfg_.search_api_key.empty()) {
      req.headers.push_back("X-Subscription-Token: " + cfg_.search_api_key);
    }
  } else {
    // Default: SearXNG JSON API.
    req.url = join_url(cfg_.search_url, "/search?q=") + url_encode(query) +
              "&format=json";
    req.headers.push_back("Accept: application/json");
    if (!cfg_.search_api_key.empty()) {
      req.headers.push_back("Authorization: Bearer " + cfg_.search_api_key);
    }
  }

  HttpResponse resp = http_(req);
  if (resp.status == 0) {
    return {false, "error: search request failed: " +
                       (resp.error.empty() ? "unknown error" : resp.error)};
  }
  if (resp.status >= 400) {
    return {false, "error: search backend returned HTTP " +
                       std::to_string(resp.status)};
  }

  json j = json::parse(resp.body, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) {
    return {false, "error: search backend returned invalid JSON"};
  }

  std::vector<SearchResult> results = parse_search_results(cfg_.search_backend, j);
  if (results.empty()) return {true, "(no results)"};

  std::string out;
  const int n = std::min<int>(max_results, static_cast<int>(results.size()));
  for (int i = 0; i < n; ++i) {
    const SearchResult& r = results[i];
    out += std::to_string(i + 1) + ". " +
           (r.title.empty() ? r.url : r.title) + "\n";
    out += "   " + r.url + "\n";
    if (!r.snippet.empty()) {
      out += "   " + truncate(r.snippet, kMaxSnippet) + "\n";
    }
  }
  return {true, out};
}

}  // namespace llmcli
