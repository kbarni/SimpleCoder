#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// A reusable, named block of instructions the user can invoke as a slash
// command (e.g. /review-diff). Loaded from a Markdown file with optional
// frontmatter.
struct Skill {
  std::string name;         // invocation name (frontmatter, else the file stem)
  std::string description;  // one-line summary shown by /skill
  std::string body;         // the instructions injected into the turn
};

// Parse a skill document: optional `---`-fenced frontmatter of `key: value`
// lines (`name` and `description` are recognised; others ignored) followed by
// the instruction body. Without frontmatter the whole text is the body. Pure;
// `name`/`description` are empty unless the frontmatter sets them.
Skill parse_skill(std::string_view text);

// A set of skills looked up by name (case-insensitive). add() keeps the first
// entry seen for a given name, so higher-priority sources are added first.
class SkillRegistry {
 public:
  void add(Skill s);
  const Skill* find(std::string_view name) const;
  const std::vector<Skill>& all() const { return skills_; }
  bool empty() const { return skills_.empty(); }

 private:
  std::vector<Skill> skills_;
};

// Skill directories in priority order: ./skills, then the user config dir's
// skills/ (e.g. ~/.config/SimpleCoder/skills).
std::vector<std::filesystem::path> skillDirs();

// Build a registry from `dirs`: every *.md file becomes a skill (name from
// frontmatter, else the file stem). Earlier directories win on a name clash;
// empty-bodied files are skipped. Reads from disk.
SkillRegistry loadSkills(const std::vector<std::filesystem::path>& dirs);

// Convenience: load from skillDirs().
SkillRegistry discoverSkills();

}  // namespace llmcli
