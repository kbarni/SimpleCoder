#include "agent/Agent.hpp"

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/Config.hpp"
#include "llm/Message.hpp"

using llmcli::Agent;
using llmcli::Config;
using llmcli::Role;

namespace {

std::string env_or(const char* key, const std::string& fallback) {
  if (const char* v = std::getenv(key); v && *v) return v;
  return fallback;
}

Config integrationCfg() {
  Config c;
  c.base_url = env_or("OPENAI_BASE_URL", "http://localhost:8080/v1");
  c.model = env_or("MODEL", "gemma-4-E2B-it-Q4_K_M.gguf");
  c.temperature = 0.0;
  c.system_prompt = "You are a terse assistant.";
  return c;
}

}  // namespace

TEST_CASE("system prompt seeds the history", "[agent]") {
  Agent agent(integrationCfg());
  REQUIRE(agent.history().size() == 1);
  CHECK(agent.history()[0].role == Role::System);
}

TEST_CASE("two turns accumulate in history with streamed deltas",
          "[agent][integration]") {
  Agent agent(integrationCfg());

  std::string streamed;
  llmcli::AgentCallbacks cb;
  cb.on_content = [&](std::string_view s) { streamed += s; };
  auto r1 = agent.send("Say the word: alpha", cb);
  if (!r1.ok && r1.http_status == 0) {
    SKIP("server not reachable: " + r1.error);
  }
  REQUIRE(r1.ok);
  CHECK_FALSE(r1.content.empty());
  CHECK(streamed == r1.content);

  // After turn 1: system, user, assistant.
  REQUIRE(agent.history().size() == 3);
  CHECK(agent.history()[1].role == Role::User);
  CHECK(agent.history()[2].role == Role::Assistant);
  CHECK(agent.history()[2].content == r1.content);

  auto r2 = agent.send("And now: beta");
  REQUIRE(r2.ok);

  // After turn 2: the first exchange is still present as context, plus the
  // new user/assistant pair.
  REQUIRE(agent.history().size() == 5);
  CHECK(agent.history()[1].content == "Say the word: alpha");
  CHECK(agent.history()[3].content == "And now: beta");
  CHECK(agent.history()[4].role == Role::Assistant);
}

TEST_CASE("compact replaces history with the summary, keeping the system prompt",
          "[agent]") {
  Config cfg;
  cfg.base_url = "http://127.0.0.1:1/v1";  // unreachable; no request is sent
  cfg.model = "x";
  cfg.system_prompt = "You are a terse assistant.";
  Agent agent(cfg);

  agent.compact_into_summary("User wants X. Decided on Y.");

  const auto& h = agent.history();
  REQUIRE(h.size() == 2);
  CHECK(h[0].role == Role::System);
  CHECK(h[0].content == "You are a terse assistant.");
  CHECK(h[1].role == Role::System);
  CHECK(h[1].content.find("User wants X. Decided on Y.") != std::string::npos);
  CHECK(agent.last_total_tokens() == 0);
}

TEST_CASE("compact without a system prompt keeps only the summary", "[agent]") {
  Config cfg;
  cfg.base_url = "http://127.0.0.1:1/v1";
  cfg.model = "x";
  Agent agent(cfg);

  agent.compact_into_summary("just the summary");

  const auto& h = agent.history();
  REQUIRE(h.size() == 1);
  CHECK(h[0].role == Role::System);
  CHECK(h[0].content.find("just the summary") != std::string::npos);
}

TEST_CASE("failed send does not leave a dangling user turn", "[agent]") {
  Config cfg;
  cfg.base_url = "http://127.0.0.1:1/v1";  // unreachable
  cfg.model = "x";
  Agent agent(cfg);

  auto res = agent.send("hello");
  CHECK_FALSE(res.ok);
  CHECK(agent.history().empty());  // no system prompt, user turn rolled back
}
