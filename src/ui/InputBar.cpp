#include "ui/InputBar.hpp"

#include <algorithm>
#include <vector>

#include "util/Utf8.hpp"

namespace llmcli {

namespace {
// Byte length of the prefix of `s` spanning at most `max_cols` code points.
std::size_t bytes_for_cols(std::string_view s, std::size_t max_cols) {
  std::size_t i = 0;
  for (std::size_t k = 0; k < max_cols && i < s.size(); ++k)
    i = utf8::next_boundary(s, i);
  return i;
}
}  // namespace

std::size_t InputBar::line_start(std::size_t pos) const {
  if (pos == 0) return 0;
  const std::size_t nl = buffer_.rfind('\n', pos - 1);
  return nl == std::string::npos ? 0 : nl + 1;
}

std::size_t InputBar::line_end(std::size_t pos) const {
  const std::size_t nl = buffer_.find('\n', pos);
  return nl == std::string::npos ? buffer_.size() : nl;
}

void InputBar::move_vertical(int dir) {
  const std::size_t ls = line_start(cursor_);
  // Current column, in code points.
  const std::size_t col = utf8::count(
      std::string_view(buffer_).substr(ls, cursor_ - ls));

  std::size_t target_start, target_end;
  if (dir < 0) {
    if (ls == 0) return;  // already on the first line
    target_start = line_start(ls - 1);
    target_end = ls - 1;
  } else {
    const std::size_t le = line_end(cursor_);
    if (le == buffer_.size()) return;  // already on the last line
    target_start = le + 1;
    target_end = line_end(target_start);
  }

  // Walk `col` code points into the target line, clamped to its end.
  std::size_t p = target_start;
  for (std::size_t k = 0; k < col && p < target_end; ++k)
    p = utf8::next_boundary(buffer_, p);
  cursor_ = p;
}

std::optional<std::string> InputBar::handle_key(int ch) {
  switch (ch) {
    case '\n':
    case '\r':
    case KEY_ENTER: {
      std::string line = buffer_;
      clear();
      return line;
    }
    case KEY_BACKSPACE:
    case 127:   // DEL
    case '\b':  // ^H
      if (cursor_ > 0) {
        const std::size_t p = utf8::prev_boundary(buffer_, cursor_);
        buffer_.erase(p, cursor_ - p);
        cursor_ = p;
      }
      return std::nullopt;
    case KEY_DC:  // Delete (forward)
      if (cursor_ < buffer_.size()) {
        const std::size_t n = utf8::next_boundary(buffer_, cursor_);
        buffer_.erase(cursor_, n - cursor_);
      }
      return std::nullopt;
    case KEY_LEFT:
      cursor_ = utf8::prev_boundary(buffer_, cursor_);
      return std::nullopt;
    case KEY_RIGHT:
      cursor_ = utf8::next_boundary(buffer_, cursor_);
      return std::nullopt;
    case KEY_HOME:
      cursor_ = line_start(cursor_);
      return std::nullopt;
    case KEY_END:
      cursor_ = line_end(cursor_);
      return std::nullopt;
    case KEY_UP:
      move_vertical(-1);
      return std::nullopt;
    case KEY_DOWN:
      move_vertical(+1);
      return std::nullopt;
    default:
      // Accept printable ASCII at the cursor; ignore other control/special
      // keys. Multibyte (UTF-8) characters arrive via insert_text() from App.
      if (ch >= 32 && ch < 127) {
        buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                       static_cast<char>(ch));
        ++cursor_;
      }
      return std::nullopt;
  }
}

void InputBar::insert_text(std::string_view s) {
  buffer_.insert(cursor_, s);
  cursor_ += s.size();
}

void InputBar::insert_newline() {
  buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_), '\n');
  ++cursor_;
}

void InputBar::clear() {
  buffer_.clear();
  cursor_ = 0;
}

int InputBar::line_count() const {
  int n = 1;
  for (char c : buffer_)
    if (c == '\n') ++n;
  return n;
}

InputBar::WrapLayout InputBar::wrap_layout(int width, int prompt_cols) const {
  WrapLayout out;
  const int w = width < 1 ? 1 : width;
  constexpr int kReserve = 1;  // keep the last column free for the cursor
  if (prompt_cols < 0) prompt_cols = 0;

  bool cursor_set = false;
  std::size_t gstart = 0;
  bool first_logical = true;

  // Walk each logical line, soft-wrapping it into one or more visual rows.
  while (true) {
    const std::size_t gend = line_end(gstart);  // next '\n', or buffer end
    std::size_t p = gstart;
    bool first_visual = true;

    do {
      const int row_index = static_cast<int>(out.rows.size());
      // The prompt only steals columns from the very first visual row.
      const int col0 = (first_logical && first_visual) ? prompt_cols : 0;
      const auto budget = static_cast<std::size_t>(std::max(1, w - kReserve - col0));

      const std::string_view rest(buffer_.data() + p, gend - p);
      const std::size_t chunk_end = p + bytes_for_cols(rest, budget);

      // Claim the cursor here if it falls inside this chunk. At a pure soft-wrap
      // boundary (chunk_end < gend) a cursor exactly at chunk_end belongs to the
      // next row's start; at a logical-line end it stays here.
      if (!cursor_set && cursor_ >= p && cursor_ <= chunk_end &&
          (cursor_ < chunk_end || chunk_end == gend)) {
        out.cursor_row = row_index;
        out.cursor_col =
            col0 + static_cast<int>(
                       utf8::count(std::string_view(buffer_).substr(
                           p, cursor_ - p)));
        cursor_set = true;
      }

      out.rows.push_back(buffer_.substr(p, chunk_end - p));
      p = chunk_end;
      first_visual = false;
    } while (p < gend);

    first_logical = false;
    if (gend == buffer_.size()) break;
    gstart = gend + 1;
  }

  if (!cursor_set) {  // defensive: pin to the last row
    out.cursor_row = std::max(0, static_cast<int>(out.rows.size()) - 1);
  }
  return out;
}

int InputBar::visual_line_count(int width, int prompt_cols) const {
  return static_cast<int>(wrap_layout(width, prompt_cols).rows.size());
}

void InputBar::render(WINDOW* win, std::string_view prompt) const {
  int height = 0, width = 0;
  getmaxyx(win, height, width);
  werase(win);
  if (height <= 0 || width <= 0) {
    wnoutrefresh(win);
    return;
  }

  const int prompt_cols = static_cast<int>(utf8::count(prompt));
  const WrapLayout wl = wrap_layout(width, prompt_cols);
  const int total = static_cast<int>(wl.rows.size());

  // Scroll vertically so the cursor row stays visible.
  int first = 0;
  if (total > height && wl.cursor_row >= height)
    first = wl.cursor_row - height + 1;

  for (int row = 0; row < height && first + row < total; ++row) {
    const int li = first + row;
    int col0 = 0;
    if (li == 0) {
      mvwaddnstr(win, row, 0, std::string(prompt).c_str(),
                 static_cast<int>(prompt.size()));
      col0 = prompt_cols;
    }
    mvwaddnstr(win, row, col0, wl.rows[li].c_str(),
               static_cast<int>(wl.rows[li].size()));
  }

  const int scr_row = wl.cursor_row - first;
  if (scr_row >= 0 && scr_row < height) {
    wmove(win, scr_row, std::min(wl.cursor_col, width - 1));
  }
  wnoutrefresh(win);
}

}  // namespace llmcli
