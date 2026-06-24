#include <catch2/catch_test_macros.hpp>

#include <string>

#include "app/Config.hpp"
#include "ui/Banner.hpp"

using namespace llmcli;

namespace {
bool any_contains(const std::vector<std::string>& lines,
                  const std::string& needle) {
  for (const auto& l : lines)
    if (l.find(needle) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST_CASE("banner shows the model and server", "[banner]") {
  Config cfg;
  cfg.base_url = "http://localhost:8080/v1";
  cfg.model = "qwen2.5-coder";

  auto lines = banner_lines(cfg);
  REQUIRE(any_contains(lines, "qwen2.5-coder"));
  REQUIRE(any_contains(lines, "http://localhost:8080/v1"));
}

TEST_CASE("banner falls back when no model is configured", "[banner]") {
  Config cfg;
  cfg.base_url = "http://localhost:8080/v1";
  cfg.model = "";

  auto lines = banner_lines(cfg);
  REQUIRE(any_contains(lines, "server default"));
}

TEST_CASE("status line reflects busy state", "[banner]") {
  Config cfg;
  cfg.base_url = "http://host/v1";
  cfg.model = "m";

  REQUIRE(status_line(cfg, false).find("[ready]") != std::string::npos);
  REQUIRE(status_line(cfg, true).find("[working") != std::string::npos);
  REQUIRE(status_line(cfg, false).find("http://host/v1") != std::string::npos);
}

TEST_CASE("status line shows context usage with a known size", "[banner]") {
  Config cfg;
  cfg.base_url = "http://host/v1";
  cfg.model = "m";

  const std::string s = status_line(cfg, false, /*total=*/12030,
                                    /*context=*/32000);
  REQUIRE(s.find("ctx 12.0k/32.0k") != std::string::npos);
  REQUIRE(s.find("(38%)") != std::string::npos);
}

TEST_CASE("status line shows raw tokens when size is unknown", "[banner]") {
  Config cfg;
  cfg.base_url = "http://host/v1";
  cfg.model = "m";

  const std::string s = status_line(cfg, false, /*total=*/450, /*context=*/0);
  REQUIRE(s.find("ctx 450") != std::string::npos);
  REQUIRE(s.find('%') == std::string::npos);  // no percent without a size
}

TEST_CASE("status line omits context segment before the first turn",
          "[banner]") {
  Config cfg;
  cfg.base_url = "http://host/v1";
  cfg.model = "m";

  REQUIRE(status_line(cfg, false).find("ctx") == std::string::npos);
}

TEST_CASE("format_token_count is compact above a thousand", "[banner]") {
  CHECK(format_token_count(0) == "0");
  CHECK(format_token_count(999) == "999");
  CHECK(format_token_count(1000) == "1.0k");
  CHECK(format_token_count(12030) == "12.0k");
}

TEST_CASE("status line shows tokens per second after a turn", "[banner]") {
  Config cfg;
  cfg.base_url = "http://host/v1";
  cfg.model = "m";

  const std::string s = status_line(cfg, false, /*total=*/100, /*context=*/0,
                                    /*tok_per_sec=*/42.5);
  CHECK(s.find("42.5 tok/s") != std::string::npos);
  // No segment when the rate is unknown.
  CHECK(status_line(cfg, false).find("tok/s") == std::string::npos);
}

TEST_CASE("context_is_full triggers only past the threshold", "[banner]") {
  CHECK_FALSE(context_is_full(8000, 10000, 0.85));   // 80% < 85%
  CHECK(context_is_full(8500, 10000, 0.85));         // exactly at 85%
  CHECK(context_is_full(9900, 10000, 0.85));         // well past
  // Unknown usage or window size, or a disabled threshold: never full.
  CHECK_FALSE(context_is_full(0, 10000, 0.85));
  CHECK_FALSE(context_is_full(9000, 0, 0.85));
  CHECK_FALSE(context_is_full(9000, 10000, 0.0));
}
