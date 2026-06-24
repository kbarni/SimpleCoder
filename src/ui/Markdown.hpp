#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// Block-level markdown classification, one entry per source line. Inline
// emphasis (bold/italic/inline code) is intentionally out of scope; see the
// deferred T21. Pure logic, no ncurses, so it is unit-testable.
enum class MdKind {
  Text,      // plain prose
  Header,    // ATX header (# .. ######); markers stripped from `text`
  Code,      // a line inside a fenced ``` block; fence lines are dropped
  ListItem,  // bullet or numbered list item; bullets normalised to "• "
};

struct MdLine {
  std::string text;
  MdKind kind = MdKind::Text;
};

// Split `text` into classified lines. Fenced code blocks (``` delimiters) take
// precedence; an unterminated fence runs to the end. The fence delimiter lines
// themselves are not emitted.
std::vector<MdLine> markdown_lines(std::string_view text);

}  // namespace llmcli
