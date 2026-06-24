#include "ui/Markdown.hpp"

#include <cctype>

namespace llmcli {

namespace {

std::string_view ltrim(std::string_view s) {
  std::size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
  return s.substr(b);
}

void classify(std::string_view line, bool& in_code, std::vector<MdLine>& out) {
  const std::string_view t = ltrim(line);

  // Fenced code: a ``` delimiter toggles code mode and is not itself emitted.
  if (t.rfind("```", 0) == 0) {
    in_code = !in_code;
    return;
  }
  if (in_code) {
    out.push_back({std::string(line), MdKind::Code});  // keep original indent
    return;
  }

  // ATX header: 1..6 '#' followed by a space.
  if (!t.empty() && t[0] == '#') {
    std::size_t h = 0;
    while (h < t.size() && t[h] == '#') ++h;
    if (h <= 6 && h < t.size() && t[h] == ' ') {
      out.push_back({std::string(ltrim(t.substr(h + 1))), MdKind::Header});
      return;
    }
  }

  // Bullet list: -, * or + followed by a space.
  if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') &&
      t[1] == ' ') {
    out.push_back({"• " + std::string(t.substr(2)), MdKind::ListItem});
    return;
  }

  // Numbered list: digits, '.', space.
  std::size_t d = 0;
  while (d < t.size() && std::isdigit(static_cast<unsigned char>(t[d]))) ++d;
  if (d > 0 && d + 1 < t.size() && t[d] == '.' && t[d + 1] == ' ') {
    out.push_back({std::string(t), MdKind::ListItem});
    return;
  }

  out.push_back({std::string(line), MdKind::Text});
}

}  // namespace

std::vector<MdLine> markdown_lines(std::string_view text) {
  std::vector<MdLine> out;
  bool in_code = false;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      classify(text.substr(start, i - start), in_code, out);
      start = i + 1;
    }
  }
  return out;
}

}  // namespace llmcli
