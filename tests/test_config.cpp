#include "app/Config.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using llmcli::Config;
using llmcli::ConfigError;
using llmcli::EnvLookup;

namespace {

// Build an EnvLookup over a fixed map, so tests need not touch the real env.
EnvLookup fakeEnv(std::map<std::string, std::string> vars) {
  return [vars = std::move(vars)](
             const char* key) -> std::optional<std::string> {
    auto it = vars.find(key);
    if (it == vars.end()) return std::nullopt;
    return it->second;
  };
}

}  // namespace

TEST_CASE("key=value fields parse into config", "[config]") {
  Config cfg;
  cfg.mergeConfString(
      "base_url = http://localhost:8080/v1\n"
      "model = qwen\n"
      "api_key = secret\n"
      "temperature = 0.2\n"
      "system_prompt = be terse\n");

  CHECK(cfg.base_url == "http://localhost:8080/v1");
  CHECK(cfg.model == "qwen");
  CHECK(cfg.api_key == "secret");
  CHECK(cfg.temperature == 0.2);
  CHECK(cfg.system_prompt == "be terse");
}

TEST_CASE("context_size parses as a non-negative integer", "[config]") {
  Config cfg;
  cfg.mergeConfString("base_url = http://x/v1\ncontext_size = 32768\n");
  CHECK(cfg.context_size == 32768);
}

TEST_CASE("context_size defaults to zero (auto-detect)", "[config]") {
  Config cfg;
  CHECK(cfg.context_size == 0);
}

TEST_CASE("a non-integer context_size is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(
      cfg.mergeConfString("base_url = x\ncontext_size = lots\n"), ConfigError);
}

TEST_CASE("max_image_bytes parses and defaults to 10 MB", "[config]") {
  Config def;
  CHECK(def.max_image_bytes == 10u * 1024 * 1024);  // default

  Config cfg;
  cfg.mergeConfString("base_url = http://x/v1\nmax_image_bytes = 2048\n");
  CHECK(cfg.max_image_bytes == 2048u);

  Config zero;
  zero.mergeConfString("base_url = http://x/v1\nmax_image_bytes = 0\n");
  CHECK(zero.max_image_bytes == 0u);  // 0 = no limit
}

TEST_CASE("a non-integer max_image_bytes is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(
      cfg.mergeConfString("base_url = x\nmax_image_bytes = big\n"), ConfigError);
}

TEST_CASE("max_tool_iterations parses and defaults sensibly", "[config]") {
  Config def;
  CHECK(def.max_tool_iterations == 50);

  Config cfg;
  cfg.mergeConfString("base_url = http://x/v1\nmax_tool_iterations = 100\n");
  CHECK(cfg.max_tool_iterations == 100);
}

TEST_CASE("a non-positive max_tool_iterations is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(
      cfg.mergeConfString("base_url = x\nmax_tool_iterations = 0\n"), ConfigError);
  REQUIRE_THROWS_AS(
      cfg.mergeConfString("base_url = x\nmax_tool_iterations = nope\n"),
      ConfigError);
}

TEST_CASE("auto-compaction defaults and parsing", "[config]") {
  Config cfg;
  CHECK(cfg.auto_compact);
  CHECK(cfg.auto_compact_threshold == 0.85);

  cfg.mergeConfString("auto_compact = false\nauto_compact_threshold = 0.5\n");
  CHECK_FALSE(cfg.auto_compact);
  CHECK(cfg.auto_compact_threshold == 0.5);
}

TEST_CASE("an out-of-range auto_compact_threshold is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(cfg.mergeConfString("auto_compact_threshold = 1.5\n"),
                    ConfigError);
  REQUIRE_THROWS_AS(cfg.mergeConfString("auto_compact_threshold = 0\n"),
                    ConfigError);
}

TEST_CASE("comments and blank lines are ignored", "[config]") {
  Config cfg;
  cfg.mergeConfString(
      "# a comment\n"
      "\n"
      "base_url = http://x/v1   # not a comment, part of the value? no\n");
  // '#' only starts a comment at the start of a (trimmed) line, so the trailing
  // text is part of the value here.
  CHECK(cfg.base_url == "http://x/v1   # not a comment, part of the value? no");
}

TEST_CASE("quoted values keep spaces and apply escapes", "[config]") {
  Config cfg;
  cfg.mergeConfString("system_prompt = \"line1\\nline2\\ttabbed  \"\n");
  CHECK(cfg.system_prompt == "line1\nline2\ttabbed  ");
}

TEST_CASE("absent optional fields keep their defaults", "[config]") {
  Config cfg;
  cfg.mergeConfString("base_url = http://localhost:8080/v1\n");

  CHECK(cfg.model.empty());
  CHECK(cfg.api_key.empty());
  CHECK(cfg.temperature == 0.7);  // default
  CHECK(cfg.system_prompt.empty());
}

TEST_CASE("unknown keys are ignored", "[config]") {
  Config cfg;
  cfg.mergeConfString("base_url = http://x/v1\nfuture_option = whatever\n");
  CHECK(cfg.base_url == "http://x/v1");
}

TEST_CASE("env vars override file values", "[config]") {
  Config cfg;
  cfg.mergeConfString(
      "base_url = http://file/v1\nmodel = file-model\napi_key = file-key\n");
  cfg.mergeEnv(fakeEnv({{"OPENAI_BASE_URL", "http://env/v1"},
                        {"OPENAI_API_KEY", "env-key"},
                        {"MODEL", "env-model"}}));

  CHECK(cfg.base_url == "http://env/v1");
  CHECK(cfg.model == "env-model");
  CHECK(cfg.api_key == "env-key");
}

TEST_CASE("local layer overrides earlier layers", "[config]") {
  Config cfg;
  cfg.mergeConfString("base_url = http://user/v1\nmodel = user-model\n");
  cfg.mergeConfString("model = local-model\n");  // later wins

  CHECK(cfg.base_url == "http://user/v1");  // untouched by local layer
  CHECK(cfg.model == "local-model");
}

TEST_CASE("missing base_url fails validation with a clear message",
          "[config]") {
  Config cfg;
  cfg.mergeConfString("model = x\n");

  try {
    cfg.validate();
    FAIL("expected ConfigError to be thrown");
  } catch (const ConfigError& e) {
    CHECK_THAT(std::string(e.what()),
               Catch::Matchers::ContainsSubstring("base_url"));
  }
}

TEST_CASE("non-numeric temperature is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(
      cfg.mergeConfString("base_url = x\ntemperature = hot\n"), ConfigError);
}

TEST_CASE("a line without '=' is rejected", "[config]") {
  Config cfg;
  REQUIRE_THROWS_AS(cfg.mergeConfString("base_url\n"), ConfigError);
}

TEST_CASE("userConfigPath honours XDG_CONFIG_HOME then HOME", "[config]") {
  CHECK(llmcli::userConfigPath(fakeEnv({{"XDG_CONFIG_HOME", "/xdg"}}))
            .string() == "/xdg/SimpleCoder/config.conf");
  CHECK(llmcli::userConfigPath(fakeEnv({{"HOME", "/home/u"}})).string() ==
        "/home/u/.config/SimpleCoder/config.conf");
}

TEST_CASE("loadConfig reads a config beside the binary", "[config]") {
  namespace fs = std::filesystem;
  const fs::path dir =
      fs::temp_directory_path() / "SimpleCoder";
  fs::create_directories(dir);
  const fs::path bin = dir / "config.conf";
  {
    std::ofstream(bin) << "base_url = http://bin/v1\nmodel = bin-model\n";
  }

  Config cfg = llmcli::loadConfig(/*userFile=*/dir / "absent.conf", bin,
                                  /*localFile=*/dir / "absent2.conf",
                                  fakeEnv({}));
  CHECK(cfg.base_url == "http://bin/v1");
  CHECK(cfg.model == "bin-model");

  fs::remove_all(dir);
}

TEST_CASE("loadConfig merges an explicit config file on top of the layers",
          "[config]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "SimpleCoder_cli_arg";
  fs::create_directories(dir);
  const fs::path bin = dir / "config.conf";
  const fs::path extra = dir / "custom.conf";
  {
    std::ofstream(bin) << "base_url = http://bin/v1\nmodel = bin-model\n";
    // The command-line file overrides discovered layers (but not env).
    std::ofstream(extra) << "model = custom-model\n";
  }

  Config cfg = llmcli::loadConfig(/*userFile=*/dir / "absent.conf", bin,
                                  /*localFile=*/dir / "absent2.conf",
                                  fakeEnv({}), extra.string().c_str());
  CHECK(cfg.base_url == "http://bin/v1");      // kept from the binary-dir layer
  CHECK(cfg.model == "custom-model");          // overridden by the CLI file

  fs::remove_all(dir);
}

TEST_CASE("loadConfig ignores an empty command-line config path", "[config]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "SimpleCoder_cli_empty";
  fs::create_directories(dir);
  const fs::path bin = dir / "config.conf";
  { std::ofstream(bin) << "base_url = http://bin/v1\n"; }

  // "" is the default when no positional arg is given — must be a no-op.
  Config cfg = llmcli::loadConfig(/*userFile=*/dir / "absent.conf", bin,
                                  /*localFile=*/dir / "absent2.conf",
                                  fakeEnv({}), "");
  CHECK(cfg.base_url == "http://bin/v1");

  fs::remove_all(dir);
}

TEST_CASE("writeStarterConfig creates a commented starter file", "[config]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "llm_cli_test_starter";
  fs::remove_all(dir);
  const fs::path target = dir / "config.conf";

  const fs::path written = llmcli::writeStarterConfig(target, fs::path{});
  CHECK(written == target);
  REQUIRE(fs::exists(target));

  std::ifstream f(target);
  const std::string body((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
  CHECK(body.find("base_url") != std::string::npos);
  CHECK(body.find('#') != std::string::npos);  // has comments

  fs::remove_all(dir);
}
