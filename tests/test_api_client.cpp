#include "llm/ApiClient.hpp"

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/Config.hpp"
#include "llm/Message.hpp"

using llmcli::ApiClient;
using llmcli::Config;
using llmcli::Message;
using llmcli::parse_model_ids;
using nlohmann::json;

TEST_CASE("parse_model_ids reads an OpenAI /v1/models payload", "[api][model]") {
  const json j = {{"object", "list"},
                  {"data", json::array({{{"id", "llama-3.1-8b"}, {"object", "model"}},
                                        {{"id", "qwen2.5-coder"}}})}};
  const auto ids = parse_model_ids(j);
  REQUIRE(ids.size() == 2);
  CHECK(ids[0] == "llama-3.1-8b");
  CHECK(ids[1] == "qwen2.5-coder");
}

TEST_CASE("parse_model_ids tolerates a missing or garbage payload",
          "[api][model]") {
  CHECK(parse_model_ids(json::object()).empty());
  CHECK(parse_model_ids(json(42)).empty());
  // Entries without a string id are skipped.
  const json j = {{"data", json::array({{{"name", "no-id"}}, {{"id", 5}}})}};
  CHECK(parse_model_ids(j).empty());
}

namespace {

std::string env_or(const char* key, const std::string& fallback) {
  if (const char* v = std::getenv(key); v && *v) return v;
  return fallback;
}

// Config pointing at the local test server. Override with OPENAI_BASE_URL /
// MODEL if your server differs.
Config integrationCfg() {
  Config c;
  c.base_url = env_or("OPENAI_BASE_URL", "http://localhost:8080/v1");
  c.model = env_or("MODEL", "gemma-4-E2B-it-Q4_K_M.gguf");
  c.temperature = 0.0;
  return c;
}

}  // namespace

TEST_CASE("streams a reply from the local server", "[api][integration]") {
  ApiClient client(integrationCfg());

  std::string streamed;
  auto res = client.chat(
      {Message::user("Reply with the single word: pong")},
      nlohmann::json::array(),
      [&](std::string_view s) { streamed += s; });

  if (!res.ok && res.http_status == 0) {
    SKIP("server not reachable: " + res.error);
  }

  INFO("error: " << res.error);
  REQUIRE(res.ok);
  CHECK(res.http_status == 200);
  CHECK_FALSE(res.content.empty());
  // The live callback must reconstruct exactly the assembled content.
  CHECK(streamed == res.content);
}

TEST_CASE("list_models returns the server's models", "[api][integration]") {
  ApiClient client(integrationCfg());
  const auto models = client.list_models();
  if (models.empty()) SKIP("server not reachable or lists no models");
  CHECK_FALSE(models.front().empty());
}

TEST_CASE("unreachable host returns a clear error and does not crash",
          "[api]") {
  Config cfg;
  cfg.base_url = "http://127.0.0.1:1/v1";  // nothing listens on port 1
  cfg.model = "x";
  ApiClient client(cfg);

  auto res = client.chat({Message::user("hi")});

  CHECK_FALSE(res.ok);
  CHECK(res.http_status == 0);
  CHECK_FALSE(res.error.empty());
}
