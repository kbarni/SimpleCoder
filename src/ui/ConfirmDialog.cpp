#include "ui/ConfirmDialog.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <ncursesw/ncurses.h>

#include "ui/TextWrap.hpp"
#include "ui/Theme.hpp"

namespace llmcli {

namespace {

// Split `s` into lines on '\n' (a trailing newline yields no extra empty line).
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  if (out.empty()) out.push_back("");
  return out;
}

// Color for a diff line, keyed on its leading marker ('+' add, '-' remove,
// '@' hunk header). Other lines render with no attribute.
attr_t diff_attr(const std::string& line) {
  if (line.empty()) return A_NORMAL;
  switch (line[0]) {
    case '+': return theme::accent_attr();  // additions: green
    case '-': return theme::error_attr();   // removals: red
    case '@': return theme::dim_attr();      // hunk separator
    default:  return A_NORMAL;               // context / summary
  }
}

}  // namespace

ConfirmChoice ConfirmDialog::ask(const std::string& tool_name,
                                 const std::string& details) const {
  constexpr int kMargin = 4;

  // A multi-line detail string carries a diff; show it wider and keep lines
  // intact (truncated to the box) so +/- columns line up. A single-line detail
  // (a command or path) is word-wrapped as before.
  const std::vector<std::string> detail_lines = split_lines(details);
  const bool is_diff = detail_lines.size() > 1;

  const int box_w = std::max(20, std::min(COLS - 2, is_diff ? 100 : 70));
  const int wrap_w = box_w - kMargin;

  std::vector<std::string> content;
  if (is_diff) {
    content = detail_lines;
  } else {
    content = wrap_text(details, wrap_w);
  }

  // Cap the detail region to fit the screen. Chrome rows: 2 borders + header +
  // blank + blank + footer = 6.
  const int max_box_h = std::max(7, LINES - 2);
  const int max_content = std::max(1, max_box_h - 6);
  if (static_cast<int>(content.size()) > max_content) {
    const int hidden = static_cast<int>(content.size()) - (max_content - 1);
    content.resize(max_content - 1);
    content.push_back("… (" + std::to_string(hidden) + " more lines)");
  }

  std::vector<std::string> lines;
  lines.push_back("Run tool: " + tool_name + "?");
  lines.push_back("");
  const std::size_t content_start = lines.size();
  for (auto& l : content) lines.push_back(l);
  const std::size_t content_end = lines.size();
  lines.push_back("");
  lines.push_back("[y] yes   [n] no   [a] always allow this session");

  const int box_h = static_cast<int>(lines.size()) + 2;
  const int top = std::max(0, (LINES - box_h) / 2);
  const int left = std::max(0, (COLS - box_w) / 2);

  WINDOW* win = newwin(box_h, box_w, top, left);
  keypad(win, TRUE);
  box(win, 0, 0);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const bool color_line =
        is_diff && i >= content_start && i < content_end;
    const attr_t attr = color_line ? diff_attr(lines[i]) : A_NORMAL;
    if (attr != A_NORMAL) wattron(win, attr);
    mvwaddnstr(win, static_cast<int>(i) + 1, 2, lines[i].c_str(), wrap_w);
    if (attr != A_NORMAL) wattroff(win, attr);
  }
  wrefresh(win);

  ConfirmChoice choice = ConfirmChoice::No;
  bool answered = false;
  while (!answered) {
    const int ch = wgetch(win);
    switch (ch) {
      case 'y':
      case 'Y':
        choice = ConfirmChoice::Yes;
        answered = true;
        break;
      case 'a':
      case 'A':
        choice = ConfirmChoice::Always;
        answered = true;
        break;
      case 'n':
      case 'N':
      case 27:  // Esc
        choice = ConfirmChoice::No;
        answered = true;
        break;
      default:
        break;  // ignore other keys
    }
  }

  delwin(win);
  // Caller is responsible for redrawing the windows underneath.
  touchwin(stdscr);
  return choice;
}

}  // namespace llmcli
