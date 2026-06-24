#include "app/Config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace llmcli {

std::optional<std::string> getenvOpt(const char* key) {
  if (const char* v = std::getenv(key); v && *v) {
    return std::string(v);
  }
  return std::nullopt;
}

namespace {

std::string_view trim(std::string_view s) {
  auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && sp(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && sp(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

// Resolve a raw value: trim, strip one matching pair of surrounding quotes, and
// apply \n \t \" \\ escapes inside quotes. Unquoted values are taken literally.
std::string parse_value(std::string_view raw) {
  raw = trim(raw);
  if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
    return std::string(raw);
  }
  const std::string_view inner = raw.substr(1, raw.size() - 2);
  std::string out;
  for (std::size_t i = 0; i < inner.size(); ++i) {
    if (inner[i] == '\\' && i + 1 < inner.size()) {
      switch (inner[++i]) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        default:
          out += '\\';
          out += inner[i];
      }
    } else {
      out += inner[i];
    }
  }
  return out;
}

// Parse a boolean: true/false, 1/0, yes/no, on/off (case-insensitive).
bool parse_bool(const std::string& val, std::string_view key) {
  std::string s;
  for (char c : val) s += static_cast<char>(std::tolower((unsigned char)c));
  if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
  if (s == "false" || s == "0" || s == "no" || s == "off") return false;
  throw ConfigError("config: '" + std::string(key) + "' must be true or false");
}

void apply_kv(Config& cfg, const std::string& key, const std::string& val) {
  if (key == "base_url")
    cfg.base_url = val;
  else if (key == "model")
    cfg.model = val;
  else if (key == "api_key")
    cfg.api_key = val;
  else if (key == "system_prompt")
    cfg.system_prompt = val;
  else if (key == "auto_compact")
    cfg.auto_compact = parse_bool(val, "auto_compact");
  else if (key == "auto_compact_threshold") {
    const char* s = val.c_str();
    char* end = nullptr;
    const double d = std::strtod(s, &end);
    if (end == s || *end != '\0' || d <= 0.0 || d > 1.0)
      throw ConfigError(
          "config: 'auto_compact_threshold' must be a number in (0, 1]");
    cfg.auto_compact_threshold = d;
  } else if (key == "allow_web")
    cfg.allow_web = parse_bool(val, "allow_web");
  else if (key == "search_url")
    cfg.search_url = val;
  else if (key == "search_api_key")
    cfg.search_api_key = val;
  else if (key == "search_backend")
    cfg.search_backend = val;
  else if (key == "mcp_config")
    cfg.mcp_config = val;
  else if (key == "allow") {
    if (!val.empty()) cfg.allow_rules.push_back(val);
  } else if (key == "deny") {
    if (!val.empty()) cfg.deny_rules.push_back(val);
  } else if (key == "temperature") {
    const char* s = val.c_str();
    char* end = nullptr;
    const double d = std::strtod(s, &end);
    if (end == s || *end != '\0')
      throw ConfigError("config: 'temperature' must be a number");
    cfg.temperature = d;
  } else if (key == "context_size") {
    const char* s = val.c_str();
    char* end = nullptr;
    const long n = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || n < 0)
      throw ConfigError(
          "config: 'context_size' must be a non-negative integer");
    cfg.context_size = static_cast<int>(n);
  } else if (key == "max_image_bytes") {
    const char* s = val.c_str();
    char* end = nullptr;
    const long long n = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0' || n < 0)
      throw ConfigError(
          "config: 'max_image_bytes' must be a non-negative integer");
    cfg.max_image_bytes = static_cast<std::size_t>(n);
  } else if (key == "max_tool_iterations") {
    const char* s = val.c_str();
    char* end = nullptr;
    const long n = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || n < 1)
      throw ConfigError(
          "config: 'max_tool_iterations' must be a positive integer");
    cfg.max_tool_iterations = static_cast<int>(n);
  }
  // Unknown keys are ignored (forward compatible).
}

}  // namespace

void Config::mergeConfString(std::string_view doc, std::string_view origin) {
  std::size_t start = 0;
  int lineno = 0;
  for (std::size_t i = 0; i <= doc.size(); ++i) {
    if (i != doc.size() && doc[i] != '\n') continue;
    ++lineno;
    const std::string_view line = trim(doc.substr(start, i - start));
    start = i + 1;
    if (line.empty() || line.front() == '#') continue;

    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      throw ConfigError("config: " + std::string(origin) + ":" +
                        std::to_string(lineno) + ": expected 'key = value'");
    }
    const std::string key(trim(line.substr(0, eq)));
    apply_kv(*this, key, parse_value(line.substr(eq + 1)));
  }
}

void Config::mergeConfFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return;  // missing optional file
  std::ifstream f(path);
  if (!f) return;
  std::ostringstream ss;
  ss << f.rdbuf();
  mergeConfString(ss.str(), path.string());
}

void Config::mergeEnv(const EnvLookup& env) {
  if (auto v = env("OPENAI_BASE_URL")) base_url = *v;
  if (auto v = env("OPENAI_API_KEY")) api_key = *v;
  if (auto v = env("MODEL")) model = *v;
}

void Config::validate() const {
  if (base_url.empty()) {
    throw ConfigError(
        "config: 'base_url' is required (set it in config.conf or via "
        "OPENAI_BASE_URL), e.g. http://localhost:8080/v1");
  }
}

std::filesystem::path userConfigPath(const EnvLookup& env) {
  namespace fs = std::filesystem;
  if (auto xdg = env("XDG_CONFIG_HOME"); xdg && !xdg->empty()) {
    return fs::path(*xdg) / "SimpleCoder" / "config.conf";
  }
  if (auto home = env("HOME"); home && !home->empty()) {
    return fs::path(*home) / ".config" / "SimpleCoder" / "config.conf";
  }
  return fs::path(".config") / "SimpleCoder" / "config.conf";
}

std::filesystem::path binaryDirConfigPath() {
  std::error_code ec;
  const std::filesystem::path exe =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) return {};
  return exe.parent_path() / "config.conf";
}

std::filesystem::path localConfigPath() { return "config.conf"; }

Config loadConfig(const std::filesystem::path& userFile,
                  const std::filesystem::path& binaryFile,
                  const std::filesystem::path& localFile,
                  const EnvLookup& env,const char* config_file) {
  Config cfg;                       // defaults
  cfg.mergeConfFile(userFile);      // user config folder
  if (!binaryFile.empty()) cfg.mergeConfFile(binaryFile);  // next to the binary
  cfg.mergeConfFile(localFile);     // current working directory
  cfg.mergeConfFile(std::filesystem::path(config_file)); // config file from command line
  cfg.mergeEnv(env);                // env overrides files
  cfg.validate();
  return cfg;
}

Config loadConfig(const char *config_file) {
  return loadConfig(userConfigPath(getenvOpt), binaryDirConfigPath(),
                    localConfigPath(), getenvOpt,config_file);
}

std::string_view exampleConfig() {
  return
      "# SimpleCoder configuration\n"
      "# Lines are `key = value`. '#' starts a comment. Values may be quoted;\n"
      "# inside quotes the escapes \\n \\t \\\" \\\\ apply. The environment\n"
      "# variables OPENAI_BASE_URL, OPENAI_API_KEY and MODEL override these.\n"
      "\n"
      "# Required: OpenAI-compatible API base URL (include the /v1 suffix).\n"
      "base_url = http://localhost:8080/v1\n"
      "\n"
      "# Model name. Many local servers accept any string or ignore it.\n"
      "model = local-model\n"
      "\n"
      "# API key. Usually unused for local servers; leave empty.\n"
      "api_key =\n"
      "\n"
      "# Sampling temperature.\n"
      "temperature = 0.7\n"
      "\n"
      "# Context window size in tokens, used for the context-usage indicator in\n"
      "# the status bar. Leave 0 to auto-detect from the server (llama.cpp\n"
      "# /props or vLLM /v1/models); set explicitly if detection fails.\n"
      "context_size = 0\n"
      "\n"
      "# Auto-compaction: when context usage passes this fraction of the window\n"
      "# after a turn, the conversation is summarized automatically (like\n"
      "# /compact) to free up room. Needs a known context_size. Set auto_compact\n"
      "# = false to disable.\n"
      "auto_compact = true\n"
      "auto_compact_threshold = 0.85\n"
      "\n"
      "# Max size in bytes of an @image attachment. Oversized images are rejected\n"
      "# (the turn isn't sent) rather than truncated. Default 10 MB; 0 = no limit.\n"
      "max_image_bytes = 10485760\n"
      "\n"
      "# Max model round-trips in one tool loop before giving up (bounds a runaway\n"
      "# request->tool->re-request cycle). Each round may run several tool calls.\n"
      "max_tool_iterations = 50\n"
      "\n"
      "# System prompt. For longer, project-specific instructions create an\n"
      "# AGENTS.md file instead (it takes precedence). Use \\n for line breaks.\n"
      "system_prompt = \"You are a helpful assistant running in a terminal.\"\n"
      "\n"
      "# Web access (opt-in). When false, the web_search and fetch_url tools are\n"
      "# not exposed to the model at all. Enabling them allows outbound network\n"
      "# requests to arbitrary hosts.\n"
      "allow_web = false\n"
      "\n"
      "# Search backend for web_search: searxng | tavily | brave.\n"
      "#   searxng: search_url is a SearXNG base URL (JSON API enabled).\n"
      "#   tavily:  search_url = https://api.tavily.com/search, set search_api_key.\n"
      "#   brave:   search_url = https://api.search.brave.com/res/v1/web/search,\n"
      "#            set search_api_key.\n"
      "search_backend = searxng\n"
      "search_url =\n"
      "search_api_key =\n"
      "\n"
      "# MCP (Model Context Protocol) servers. Point this at a sidecar JSON file\n"
      "# that lists external tool servers; their tools are exposed to the model\n"
      "# (confirm-required). The file uses the familiar shape:\n"
      "#   { \"mcpServers\": {\n"
      "#       \"filesystem\": { \"command\": \"npx\",\n"
      "#         \"args\": [\"-y\", \"@modelcontextprotocol/server-filesystem\", \"/data\"] },\n"
      "#       \"git\": { \"command\": \"uvx\", \"args\": [\"mcp-server-git\"] } } }\n"
      "# Leave empty to disable MCP.\n"
      "mcp_config =\n"
      "\n"
      "# Tool allow-list policy. Auto-approve or block gated tools (write_file,\n"
      "# run_bash, …) so they don't prompt every time. Repeat `allow`/`deny` to\n"
      "# add rules; each is a bare tool name (whole tool) or `<tool>:<prefix>`\n"
      "# (matches when the command/target starts with <prefix>). `deny` wins over\n"
      "# `allow`. Read-only tools never prompt and are unaffected.\n"
      "#   allow = read_file\n"
      "#   allow = run_bash:git status\n"
      "#   allow = run_bash:ls\n"
      "#   deny  = run_bash:rm\n";
}

namespace {
std::filesystem::path try_write(const std::filesystem::path& p) {
  std::error_code ec;
  if (!p.parent_path().empty())
    std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p);
  if (!f) return {};
  f << exampleConfig();
  if (!f) return {};
  return p;
}
}  // namespace

std::filesystem::path writeStarterConfig(const std::filesystem::path& preferred,
                                         const std::filesystem::path& fallback) {
  if (auto p = try_write(preferred); !p.empty()) return p;
  if (!fallback.empty())
    if (auto p = try_write(fallback); !p.empty()) return p;
  return {};
}

}  // namespace llmcli
