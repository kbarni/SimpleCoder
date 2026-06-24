#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#include "app/SystemPrompt.hpp"

using namespace llmcli;

namespace {
FileReader table_reader(std::map<std::string, std::string> files) {
  return [files = std::move(files)](
             const std::string& p) -> std::optional<std::string> {
    auto it = files.find(p);
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
}
}  // namespace

TEST_CASE("an existing AGENTS.md overrides the config prompt",
          "[systemprompt]") {
  auto r = resolveSystemPrompt("config prompt", {"AGENTS.md"},
                               table_reader({{"AGENTS.md", "project rules"}}));
  CHECK(r == "project rules");
}

TEST_CASE("absent AGENTS.md falls back to the config prompt",
          "[systemprompt]") {
  auto r = resolveSystemPrompt("config prompt", {"AGENTS.md"},
                               table_reader({}));
  CHECK(r == "config prompt");
}

TEST_CASE("an empty AGENTS.md does not clobber the config prompt",
          "[systemprompt]") {
  auto r = resolveSystemPrompt("config prompt", {"AGENTS.md"},
                               table_reader({{"AGENTS.md", ""}}));
  CHECK(r == "config prompt");
}

TEST_CASE("candidates are tried in order", "[systemprompt]") {
  auto r = resolveSystemPrompt(
      "config", {"AGENTS.md", "/bin/AGENTS.md"},
      table_reader({{"/bin/AGENTS.md", "from binary dir"}}));
  CHECK(r == "from binary dir");  // cwd missing, binary-dir used
}

TEST_CASE("stripCodeFence unwraps a fenced block", "[systemprompt]") {
  CHECK(stripCodeFence("```markdown\n# Title\nbody\n```") == "# Title\nbody\n");
  CHECK(stripCodeFence("```\nplain\n```") == "plain\n");
}

TEST_CASE("stripCodeFence leaves unfenced text alone", "[systemprompt]") {
  CHECK(stripCodeFence("# Title\nno fences") == "# Title\nno fences");
}

TEST_CASE("initPrompt embeds the file listing", "[systemprompt]") {
  auto p = initPrompt("src/main.cpp\nREADME.md\n");
  CHECK(p.find("AGENTS.md") != std::string::npos);
  CHECK(p.find("src/main.cpp") != std::string::npos);
}

TEST_CASE("compactPrompt asks for a summary of the conversation",
          "[systemprompt]") {
  auto p = compactPrompt();
  CHECK(p.find("Summarize") != std::string::npos);
  CHECK_FALSE(p.empty());
}
