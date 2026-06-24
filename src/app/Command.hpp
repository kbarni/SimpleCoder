#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// A line typed at the prompt is either an ordinary chat message or a slash
// command. Parsing is pure (no UI/agent state) so dispatch is unit-testable;
// App performs the side effects.
enum class CommandKind {
  None,     // not a command — send the line as a chat message
  Help,     // /help — show the command/key overlay
  Clear,    // /clear — reset the conversation and view
  Retry,    // /retry — resend the last user message
  Model,    // /model <name> — switch model (arg = name, may be empty)
  Init,     // /init — generate an AGENTS.md from the project context
  Compact,  // /compact — summarize the conversation to shrink the context
  Skill,    // /skill [name [args]] — list skills, or run one (arg = the rest)
  Quit,     // /quit or /exit
  Unknown,  // looked like a command but is unrecognised (arg = the word typed)
};

struct Command {
  CommandKind kind = CommandKind::None;
  std::string arg;  // Model: the requested name; Unknown: the command word
};

// Classify a submitted input line. A line not beginning with '/' is None.
// Leading/trailing whitespace is ignored; the command word is case-insensitive.
Command parse_command(std::string_view line);

// Lines shown in the /help (and F1) overlay: available commands and key bindings.
std::vector<std::string> help_lines();

}  // namespace llmcli
