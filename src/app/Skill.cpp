#include "app/Skill.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "app/Config.hpp"  // userConfigPath

namespace llmcli {

namespace fs = std::filesystem;

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

// Strip one matching pair of surrounding single/double quotes, if present.
std::string_view unquote(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') &&
      s.back() == s.front()) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

// The [start, end) of the line beginning at `pos` and the start of the next.
struct LineSpan {
  std::size_t begin, end, next;
};
LineSpan next_line(std::string_view text, std::size_t pos) {
  const std::size_t nl = text.find('\n', pos);
  if (nl == std::string_view::npos) return {pos, text.size(), text.size()};
  return {pos, nl, nl + 1};
}

}  // namespace

Skill parse_skill(std::string_view text) {
  Skill s;

  // Find the first non-blank line; only a leading "---" opens frontmatter.
  std::size_t scan = 0;
  LineSpan first{0, 0, 0};
  bool found = false;
  while (scan < text.size()) {
    first = next_line(text, scan);
    if (!trim(text.substr(first.begin, first.end - first.begin)).empty()) {
      found = true;
      break;
    }
    scan = first.next;
  }

  if (!found ||
      trim(text.substr(first.begin, first.end - first.begin)) != "---") {
    s.body = std::string(trim(text));
    return s;
  }

  // Parse frontmatter `key: value` lines until the closing "---".
  std::size_t pos = first.next;
  bool closed = false;
  while (pos < text.size()) {
    const LineSpan ls = next_line(text, pos);
    const std::string_view line =
        trim(text.substr(ls.begin, ls.end - ls.begin));
    pos = ls.next;
    if (line == "---") {  // end of frontmatter
      closed = true;
      break;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string key = lower(trim(line.substr(0, colon)));
    const std::string_view val = unquote(trim(line.substr(colon + 1)));
    if (key == "name")
      s.name = std::string(val);
    else if (key == "description")
      s.description = std::string(val);
  }

  if (!closed) {
    // Malformed frontmatter (no closing fence): treat the whole document as the
    // body rather than silently swallowing it.
    return Skill{"", "", std::string(trim(text))};
  }

  s.body = std::string(trim(text.substr(pos)));
  return s;
}

void SkillRegistry::add(Skill s) {
  if (find(s.name)) return;  // first source wins
  skills_.push_back(std::move(s));
}

const Skill* SkillRegistry::find(std::string_view name) const {
  const std::string n = lower(name);
  for (const Skill& s : skills_) {
    if (lower(s.name) == n) return &s;
  }
  return nullptr;
}

std::vector<fs::path> skillDirs() {
  std::vector<fs::path> dirs;
  dirs.emplace_back("skills");  // current working directory
  const fs::path user_cfg = userConfigPath();  // .../SimpleCoder/config.conf
  if (!user_cfg.empty()) dirs.push_back(user_cfg.parent_path() / "skills");
  return dirs;
}

SkillRegistry loadSkills(const std::vector<fs::path>& dirs) {
  SkillRegistry reg;
  for (const fs::path& dir : dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (entry.is_regular_file(ec) && entry.path().extension() == ".md") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());  // deterministic load order

    for (const fs::path& f : files) {
      std::ifstream in(f, std::ios::binary);
      if (!in) continue;
      std::ostringstream ss;
      ss << in.rdbuf();
      Skill s = parse_skill(ss.str());
      if (s.name.empty()) s.name = f.stem().string();
      if (s.body.empty()) continue;  // nothing to inject
      reg.add(std::move(s));
    }
  }
  return reg;
}

SkillRegistry discoverSkills() { return loadSkills(skillDirs()); }

}  // namespace llmcli
