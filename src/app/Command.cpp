#include "app/Command.hpp"

#include <algorithm>
#include <cctype>

namespace llmcli {

namespace {

std::string_view trim(std::string_view s) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

}  // namespace

Command parse_command(std::string_view line) {
  std::string_view s = trim(line);
  if (s.empty() || s.front() != '/') return {CommandKind::None, std::string(s)};

  s.remove_prefix(1);  // drop the leading '/'
  const std::size_t sp = s.find_first_of(" \t");
  const std::string word = lower(s.substr(0, sp));
  const std::string rest =
      sp == std::string_view::npos ? std::string{}
                                   : std::string(trim(s.substr(sp)));

  if (word == "help" || word == "?") return {CommandKind::Help, {}};
  if (word == "clear") return {CommandKind::Clear, {}};
  if (word == "retry") return {CommandKind::Retry, {}};
  if (word == "model") return {CommandKind::Model, rest};
  if (word == "init") return {CommandKind::Init, {}};
  if (word == "compact") return {CommandKind::Compact, {}};
  if (word == "skill" || word == "skills") return {CommandKind::Skill, rest};
  if (word == "quit" || word == "exit") return {CommandKind::Quit, {}};
  return {CommandKind::Unknown, word};
}

std::vector<std::string> help_lines() {
  return {
      "Commands",
      "  /help            show this help",
      "  /clear           clear the conversation",
      "  /retry           resend the last message",
      "  /model [name]    list models, or switch to one",
      "  /init            generate AGENTS.md from this project",
      "  /compact         summarize the conversation to free up context",
      "  /skill [name]    list skills, or run one (also /<name>)",
      "  /quit            exit (also /exit)",
      "",
      "Keys",
      "  Enter            send message",
      "  Alt+Enter        insert a newline",
      "  Up / Down        scroll (move cursor while typing)",
      "  Left / Right     move cursor",
      "  Home / End       top / bottom (line start/end while typing)",
      "  PgUp / PgDn      scroll one page",
      "  Tab              show / hide last reasoning or tool output",
      "  Esc              cancel the in-flight response",
      "  Mouse wheel      scroll",
      "  F1               this help",
      "",
      "  Press any key to close.",
  };
}

}  // namespace llmcli
