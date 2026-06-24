#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// Thrown when configuration is missing required fields or otherwise invalid.
class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Looks up an environment variable; returns nullopt if unset.
using EnvLookup = std::function<std::optional<std::string>(const char*)>;

// Default lookup backed by std::getenv.
std::optional<std::string> getenvOpt(const char* key);

struct Config {
  std::string base_url;       // required, e.g. http://localhost:8080/v1
  std::string model;          // optional (some local servers ignore it)
  std::string api_key;        // optional (often unused locally)
  double temperature = 0.7;   // optional
  std::string system_prompt;  // optional
  int context_size = 0;       // optional; 0 = auto-detect from the server

  // Auto-compaction: when context usage crosses `auto_compact_threshold` (a
  // fraction of the window) after a turn, automatically run /compact. Requires a
  // known context_size. Disabled by setting auto_compact = false.
  bool auto_compact = true;
  double auto_compact_threshold = 0.85;

  // Max size in bytes of an `@image` attachment (T34). Oversized images are
  // rejected (the turn isn't sent) rather than truncated; base64 inflates by
  // ~33% and the image re-sends every turn until /compact. 0 = no limit.
  std::size_t max_image_bytes = 10 * 1024 * 1024;  // 10 MB

  // Max model round-trips in one tool loop before giving up, bounding a runaway
  // request→tool→re-request cycle. Each round may run several tool calls, so this
  // is round-trips, not tool calls. Must be >= 1.
  int max_tool_iterations = 50;

  // Web access (opt-in). When allow_web is false the web tools are not exposed.
  bool allow_web = false;
  std::string search_url;                 // search backend endpoint
  std::string search_api_key;             // search backend API key, if needed
  std::string search_backend = "searxng"; // searxng | tavily | brave

  // MCP (T28): path to a sidecar JSON file listing external MCP servers (the
  // flat key=value parser can't express a per-server command + args array). The
  // file uses the familiar { "mcpServers": { "<name>": { "command", "args",
  // "env" } } } shape. Empty = no MCP servers. Their tools are confirm-required.
  std::string mcp_config;

  // Tool allow-list policy (T30): auto-approve or block gated tool calls without
  // prompting. Each `allow`/`deny` line in config appends one rule; rules
  // accumulate across keys and config layers. A rule is a bare tool name (whole
  // tool) or `<tool>:<prefix>` (e.g. `run_bash:git status`). Deny wins.
  std::vector<std::string> allow_rules;
  std::vector<std::string> deny_rules;

  // Parse a `key = value` document and merge present keys. Absent keys are left
  // untouched. Lines: `#` comments and blanks are ignored; split on the first
  // `=`; both sides trimmed; one surrounding pair of quotes is stripped and the
  // escapes \n \t \" \\ apply inside quotes; unknown keys are ignored. Throws
  // ConfigError on a non-empty line with no `=` or a non-numeric temperature.
  void mergeConfString(std::string_view doc, std::string_view origin = "<string>");

  // Read and merge a config file if it exists. Missing file is a no-op.
  // Throws ConfigError on a parse error.
  void mergeConfFile(const std::filesystem::path& path);

  // Override fields from environment variables:
  //   OPENAI_BASE_URL -> base_url, OPENAI_API_KEY -> api_key, MODEL -> model.
  void mergeEnv(const EnvLookup& env);

  // Throws ConfigError if a required field is missing or invalid.
  void validate() const;
};

// $XDG_CONFIG_HOME/SimpleCoder/config.conf, else ~/.config/SimpleCoder/config.conf.
std::filesystem::path userConfigPath(const EnvLookup& env = getenvOpt);

// config.conf next to the running executable (Linux: via /proc/self/exe).
// Empty path if the executable directory can't be resolved.
std::filesystem::path binaryDirConfigPath();

// ./config.conf in the current working directory.
std::filesystem::path localConfigPath();

// Full load: defaults -> user file -> binary-dir file -> local file -> env,
// then validate. Later layers override earlier ones.
Config loadConfig(const std::filesystem::path& userFile,
                  const std::filesystem::path& binaryFile,
                  const std::filesystem::path& localFile, const EnvLookup& env,
                  const char* config_file = "");

// Convenience overload using the default paths and process environment. An
// optional extra config file (e.g. one passed on the command line) is merged
// last, after the standard layers and before env overrides.
Config loadConfig(const char* config_file = "");

// Write a commented starter config to `preferred`, falling back to `fallback`
// if that location can't be written (e.g. a read-only install dir). Returns the
// path actually written, or an empty path if neither could be written.
std::filesystem::path writeStarterConfig(const std::filesystem::path& preferred,
                                         const std::filesystem::path& fallback);

// The contents written by writeStarterConfig (also shipped as
// config.example.conf).
std::string_view exampleConfig();

}  // namespace llmcli
