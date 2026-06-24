#include "ui/Banner.hpp"

#include <cstdio>

namespace llmcli {

std::string format_token_count(int tokens) {
  if (tokens < 1000) return std::to_string(tokens);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1fk", tokens / 1000.0);
  return buf;
}

std::vector<std::string> banner_lines(const Config& cfg) {
  const std::string model = cfg.model.empty() ? "(server default)" : cfg.model;
  return {
      R"(  __    ____    ____  _                 _       ____          _)",
      R"( / /   / /\ \  / ___|(_)_ __ ___  _ __ | | ___ / ___|___   __| | ___ _ __ )",
      R"(/ /   / /  \ \ \___ \| | '_ ` _ \| '_ \| |/ _ \ |   / _ \ / _` |/ _ \ '__| model:  )" + model,
      R"(\ \  / /   / /  ___) | | | | | | | |_) | |  __/ |__| (_) | (_| |  __/ |)",
      R"( \_\/_/   /_/  |____/|_|_| |_| |_| .__/|_|\___|\____\___/ \__,_|\___|_|    server: )" + cfg.base_url,
      R"(                                 |_|)",
      "",
      "  Type a message and press Enter.  F1 for help * ↑/↓ to scroll * /quit to close.",
  };
}

std::string status_line(const Config& cfg, bool busy, int total_tokens,
                        int context_size, double tok_per_sec) {
  const std::string model = cfg.model.empty() ? "default" : cfg.model;
  std::string s = " </>SimpleCoder  ";
  s += model;
  s += "  @ ";
  s += cfg.base_url;
  s += busy ? "   [working…]" : "   [ready]";

  if (total_tokens > 0) {
    s += "   ctx " + format_token_count(total_tokens);
    if (context_size > 0) {
      s += "/" + format_token_count(context_size);
      const int pct = (total_tokens * 100 + context_size / 2) / context_size;
      s += " (" + std::to_string(pct) + "%)";
    }
  }
  if (tok_per_sec > 0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "   %.1f tok/s", tok_per_sec);
    s += buf;
  }
  return s;
}

bool context_is_full(int used_tokens, int context_size, double threshold) {
  if (used_tokens <= 0 || context_size <= 0 || threshold <= 0.0) return false;
  return static_cast<double>(used_tokens) >= threshold * context_size;
}

}  // namespace llmcli
