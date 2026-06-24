#include "agent/Tool.hpp"

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/Config.hpp"
#include "net/Html.hpp"
#include "net/HttpClient.hpp"

using namespace llmcli;
using nlohmann::json;

namespace {

// Build a stub HttpFn that records the last request and returns a canned reply.
struct StubHttp {
  HttpResponse reply;
  HttpRequest last;
  HttpFn fn() {
    return [this](const HttpRequest& req) {
      last = req;
      return reply;
    };
  }
};

}  // namespace

// --- html_to_text ----------------------------------------------------------

TEST_CASE("html_to_text strips tags and decodes entities", "[web][html]") {
  const std::string html =
      "<html><head><title>x</title><style>a{color:red}</style></head>"
      "<body><h1>Hello</h1><p>World &amp; <b>friends</b></p>"
      "<script>evil()</script></body></html>";
  const std::string text = html_to_text(html, "text/html");
  CHECK(text.find("Hello") != std::string::npos);
  CHECK(text.find("World & friends") != std::string::npos);
  CHECK(text.find("evil") == std::string::npos);   // script dropped
  CHECK(text.find("color:red") == std::string::npos);  // style dropped
  CHECK(text.find('<') == std::string::npos);       // no markup left
}

TEST_CASE("html_to_text collapses whitespace", "[web][html]") {
  const std::string text =
      html_to_text("<p>a   \n\n   b</p>\n\n\n\n<p>c</p>", "text/html");
  CHECK(text.find("a b") != std::string::npos);
  CHECK(text.find("\n\n\n") == std::string::npos);  // squeezed to <= 2
}

TEST_CASE("html_to_text passes non-HTML bodies through", "[web][html]") {
  const std::string body = R"({"a": 1, "b": "<not a tag>"})";
  CHECK(html_to_text(body, "application/json") == body);
  CHECK(html_to_text("plain & simple", "text/plain") == "plain & simple");
}

// --- fetch_url -------------------------------------------------------------

TEST_CASE("fetch_url returns stripped text", "[web][fetch]") {
  StubHttp stub;
  stub.reply = {200, "<p>hi there</p>", "text/html", "", false};
  FetchUrlTool tool(stub.fn());
  auto res = tool.execute({{"url", "https://example.com"}});
  REQUIRE(res.ok);
  CHECK(res.output.find("hi there") != std::string::npos);
  CHECK(stub.last.url == "https://example.com");
}

TEST_CASE("fetch_url truncates at max_chars", "[web][fetch]") {
  StubHttp stub;
  stub.reply = {200, std::string(1000, 'x'), "text/plain", "", false};
  FetchUrlTool tool(stub.fn());
  auto res = tool.execute({{"url", "https://example.com"}, {"max_chars", 10}});
  REQUIRE(res.ok);
  CHECK(res.output.find("[truncated]") != std::string::npos);
  CHECK(res.output.size() < 100);
}

TEST_CASE("fetch_url reports a transport failure", "[web][fetch]") {
  StubHttp stub;
  stub.reply = {0, "", "", "could not connect", false};
  FetchUrlTool tool(stub.fn());
  auto res = tool.execute({{"url", "https://example.com"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("could not connect") != std::string::npos);
}

TEST_CASE("fetch_url reports an HTTP error status", "[web][fetch]") {
  StubHttp stub;
  stub.reply = {404, "<h1>Not Found</h1>", "text/html", "", false};
  FetchUrlTool tool(stub.fn());
  auto res = tool.execute({{"url", "https://example.com/missing"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("404") != std::string::npos);
}

TEST_CASE("fetch_url rejects a non-absolute URL", "[web][fetch]") {
  FetchUrlTool tool(StubHttp{}.fn());
  auto res = tool.execute({{"url", "example.com"}});
  CHECK_FALSE(res.ok);
}

// --- parse_search_results --------------------------------------------------

TEST_CASE("parse_search_results maps a SearXNG payload", "[web][search]") {
  json j = {{"results",
             json::array({{{"title", "T1"}, {"url", "https://a"},
                           {"content", "snip1"}},
                          {{"title", "T2"}, {"url", "https://b"},
                           {"content", "snip2"}}})}};
  auto r = parse_search_results("searxng", j);
  REQUIRE(r.size() == 2);
  CHECK(r[0].title == "T1");
  CHECK(r[0].url == "https://a");
  CHECK(r[0].snippet == "snip1");
}

TEST_CASE("parse_search_results maps a Brave payload", "[web][search]") {
  json j = {{"web",
             {{"results",
               json::array({{{"title", "B"}, {"url", "https://x"},
                             {"description", "d"}}})}}}};
  auto r = parse_search_results("brave", j);
  REQUIRE(r.size() == 1);
  CHECK(r[0].title == "B");
  CHECK(r[0].snippet == "d");
}

TEST_CASE("parse_search_results drops results without a URL", "[web][search]") {
  json j = {{"results", json::array({{{"title", "no url"}}})}};
  CHECK(parse_search_results("searxng", j).empty());
}

TEST_CASE("parse_search_results tolerates a garbage payload", "[web][search]") {
  CHECK(parse_search_results("searxng", json::array()).empty());
  CHECK(parse_search_results("searxng", json(42)).empty());
}

// --- web_search ------------------------------------------------------------

TEST_CASE("web_search errors when no search_url is configured",
          "[web][search]") {
  WebToolsConfig cfg;  // search_url empty
  WebSearchTool tool(cfg, StubHttp{}.fn());
  auto res = tool.execute({{"query", "anything"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("not configured") != std::string::npos);
}

TEST_CASE("web_search formats results from the backend", "[web][search]") {
  StubHttp stub;
  json body = {{"results",
                json::array({{{"title", "First"}, {"url", "https://one"},
                              {"content", "about one"}}})}};
  stub.reply = {200, body.dump(), "application/json", "", false};

  WebToolsConfig cfg;
  cfg.search_url = "https://searx.example";
  cfg.search_backend = "searxng";
  WebSearchTool tool(cfg, stub.fn());

  auto res = tool.execute({{"query", "hello world"}});
  REQUIRE(res.ok);
  CHECK(res.output.find("First") != std::string::npos);
  CHECK(res.output.find("https://one") != std::string::npos);
  // The query is URL-encoded into the SearXNG endpoint.
  CHECK(stub.last.url.find("q=hello%20world") != std::string::npos);
  CHECK(stub.last.url.find("format=json") != std::string::npos);
}

TEST_CASE("web_search posts JSON for the tavily backend", "[web][search]") {
  StubHttp stub;
  stub.reply = {200, R"({"results":[]})", "application/json", "", false};
  WebToolsConfig cfg;
  cfg.search_url = "https://api.tavily.com/search";
  cfg.search_backend = "tavily";
  cfg.search_api_key = "secret";
  WebSearchTool tool(cfg, stub.fn());

  auto res = tool.execute({{"query", "q"}});
  CHECK(res.ok);
  CHECK(stub.last.method == "POST");
  CHECK(stub.last.body.find("\"secret\"") != std::string::npos);
  CHECK(stub.last.body.find("\"q\"") != std::string::npos);
}

// --- registration / config -------------------------------------------------

TEST_CASE("default_tools omits web tools unless enabled", "[web][tools]") {
  auto without = default_tools();
  bool has_web = false;
  for (auto& t : without)
    if (t->name() == "web_search" || t->name() == "fetch_url") has_web = true;
  CHECK_FALSE(has_web);

  WebToolsConfig cfg;
  cfg.enabled = true;
  cfg.search_url = "https://searx.example";
  auto with = default_tools(cfg);
  int web_count = 0;
  for (auto& t : with)
    if (t->name() == "web_search" || t->name() == "fetch_url") ++web_count;
  CHECK(web_count == 2);
}

TEST_CASE("config parses web settings", "[web][config]") {
  Config cfg;
  cfg.mergeConfString(
      "allow_web = true\n"
      "search_backend = tavily\n"
      "search_url = https://api.tavily.com/search\n"
      "search_api_key = abc123\n");
  CHECK(cfg.allow_web);
  CHECK(cfg.search_backend == "tavily");
  CHECK(cfg.search_url == "https://api.tavily.com/search");
  CHECK(cfg.search_api_key == "abc123");
}

TEST_CASE("config rejects a non-boolean allow_web", "[web][config]") {
  Config cfg;
  CHECK_THROWS_AS(cfg.mergeConfString("allow_web = maybe\n"), ConfigError);
}

TEST_CASE("url_encode percent-encodes reserved characters", "[web][http]") {
  CHECK(url_encode("a b&c") == "a%20b%26c");
  CHECK(url_encode("safe-_.~") == "safe-_.~");
}

// Optional live check: hits a real search backend if one is configured via the
// environment, and auto-skips otherwise (mirrors the API integration tests).
TEST_CASE("web_search against a live backend", "[web][integration]") {
  const char* url = std::getenv("SEARCH_URL");
  if (!url || !*url) SKIP("set SEARCH_URL to run the live web_search test");

  WebToolsConfig cfg;
  cfg.search_url = url;
  if (const char* b = std::getenv("SEARCH_BACKEND")) cfg.search_backend = b;
  if (const char* k = std::getenv("SEARCH_API_KEY")) cfg.search_api_key = k;

  WebSearchTool tool(cfg);  // real http_request
  auto res = tool.execute({{"query", "wikipedia"}});
  if (!res.ok && res.output.find("failed") != std::string::npos) {
    SKIP("search backend unreachable: " + res.output);
  }
  CHECK(res.ok);
}
