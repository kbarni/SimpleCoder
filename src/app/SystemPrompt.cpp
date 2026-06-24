#include "app/SystemPrompt.hpp"

#include <fstream>
#include <sstream>

#include "app/Config.hpp"  // binaryDirConfigPath

namespace llmcli {

std::vector<std::filesystem::path> agentsMdPaths() {
  std::vector<std::filesystem::path> v;
  v.emplace_back("AGENTS.md");  // current working directory
  const std::filesystem::path bin = binaryDirConfigPath();
  if (!bin.empty()) v.push_back(bin.parent_path() / "AGENTS.md");
  return v;
}

std::string resolveSystemPrompt(
    std::string_view configPrompt,
    const std::vector<std::filesystem::path>& candidates,
    const FileReader& reader) {
  for (const auto& p : candidates) {
    if (auto c = reader(p.string()); c && !c->empty()) return *c;
  }
  return std::string(configPrompt);
}

std::string resolveSystemPrompt(std::string_view configPrompt) {
  const FileReader disk = [](const std::string& p) -> std::optional<std::string> {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  return resolveSystemPrompt(configPrompt, agentsMdPaths(), disk);
}

std::string initPrompt(std::string_view file_listing) {
  std::string p =
      "Generate the contents of an AGENTS.md file for this project. It will be "
      "used as system-prompt guidance for an AI coding assistant working in "
      "this repository. Cover, concisely: what the project is, how to "
      "build/test/run it, and important conventions or constraints. Output ONLY "
      "the markdown contents, with no preamble and no surrounding code fences.\n\n"
      "Project files:\n";
  p += file_listing;
  return p;
}

std::string compactPrompt() {
  return
      "Summarize our conversation so far so the summary can replace the full "
      "history as your working context for continuing. Capture, concisely but "
      "completely: the user's goals and constraints, key facts and decisions, "
      "important file paths and any code changes made, and any open or pending "
      "tasks. Write it as notes to yourself for resuming the work. Output ONLY "
      "the summary, with no preamble and no surrounding code fences.";
}

std::string stripCodeFence(std::string s) {
  const std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return s;
  if (s.compare(b, 3, "```") != 0) return s;

  // Drop the opening fence line (``` plus an optional language label).
  const std::size_t nl = s.find('\n', b);
  if (nl == std::string::npos) return s;
  std::string inner = s.substr(nl + 1);

  // Drop a trailing fence line, if present.
  const std::size_t e = inner.find_last_not_of(" \t\r\n");
  if (e != std::string::npos && e >= 2 && inner.compare(e - 2, 3, "```") == 0) {
    const std::size_t nl_before = inner.rfind('\n', e - 3);
    // Drop the fence line but keep the newline that ends the real content.
    inner.erase(nl_before == std::string::npos ? 0 : nl_before + 1);
  }
  return inner;
}

}  // namespace llmcli
