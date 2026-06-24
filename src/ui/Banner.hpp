#pragma once

#include <string>
#include <vector>

#include "app/Config.hpp"

namespace llmcli {

// Pure helpers for the startup banner and status header. No ncurses calls, so
// both are unit-testable headless.

// ASCII-art logo plus model / server info lines, shown as the opening chat
// entry. The returned lines include the configured model and base URL.
std::vector<std::string> banner_lines(const Config& cfg);

// One-line status header text: program name, model, server, and a busy/idle
// indicator. Truncated by the caller to the window width.
//
// When `total_tokens` > 0 a context-usage segment is appended: with a known
// `context_size` it reads e.g. "ctx 12k/32k (38%)", otherwise just the token
// count "ctx 12k". Both default to 0 (no segment) before the first turn.
// When `tok_per_sec` > 0 a "42.0 tok/s" segment for the last turn is appended.
std::string status_line(const Config& cfg, bool busy, int total_tokens = 0,
                        int context_size = 0, double tok_per_sec = 0);

// Whether context usage has reached `threshold` (a fraction in (0,1]) of the
// window — the trigger for auto-compaction. False when the window size or usage
// is unknown (<= 0) or the threshold is non-positive. Pure; unit-tested.
bool context_is_full(int used_tokens, int context_size, double threshold);

// Format a token count compactly: counts below 1000 verbatim, larger ones as a
// one-decimal "k" value (e.g. 12030 -> "12.0k"). Exposed for testing.
std::string format_token_count(int tokens);

}  // namespace llmcli
