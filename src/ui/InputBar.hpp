#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ncursesw/ncurses.h>

namespace llmcli {

// Multi-line text input with a cursor. Key handling is pure logic (no ncurses
// calls), so it is unit-testable; only `render` touches the window. The buffer
// may contain '\n' (inserted via Alt-Enter at the App level); Enter submits.
class InputBar {
 public:
  // Process one key. On Enter, returns the whole buffer and clears it; for edits
  // and cursor movement (arrows, Home/End, Backspace, printable ASCII) returns
  // nullopt. Other keys are ignored.
  std::optional<std::string> handle_key(int ch);

  // Insert a newline at the cursor (Alt-Enter) without submitting.
  void insert_newline();

  // Insert UTF-8 text (a typed multibyte character) at the cursor.
  void insert_text(std::string_view s);

  const std::string& buffer() const { return buffer_; }
  std::size_t cursor() const { return cursor_; }  // byte index, for tests
  void clear();

  // Number of logical (newline-separated) lines, at least 1.
  int line_count() const;

  // The buffer laid out into visual rows for a window `width`, with the prompt
  // occupying `prompt_cols` columns on the first row. Logical lines longer than
  // the available width soft-wrap onto further rows; the cursor's on-screen
  // position is reported too. Pure (no ncurses); shared by render and sizing and
  // exposed for testing.
  struct WrapLayout {
    std::vector<std::string> rows;  // visual rows, verbatim (no '\n')
    int cursor_row = 0;
    int cursor_col = 0;  // on-screen column (includes the prompt on row 0)
  };
  WrapLayout wrap_layout(int width, int prompt_cols) const;

  // Number of visual rows the buffer occupies at the given geometry (>= 1).
  // Used to size the input box so it grows with both newlines and soft wraps.
  int visual_line_count(int width, int prompt_cols) const;

  // Draw `prompt` + buffer into `win`, placing the cursor. The prompt prefixes
  // the first logical line; later lines start at column 0. If the buffer has
  // more lines than the window is tall, the tail (around the cursor) is shown.
  void render(WINDOW* win, std::string_view prompt = "> ") const;

 private:
  // Byte index of the start / end of the logical line containing `pos`.
  std::size_t line_start(std::size_t pos) const;
  std::size_t line_end(std::size_t pos) const;
  // Move the cursor up (-1) or down (+1) one logical line, keeping the column.
  void move_vertical(int dir);

  std::string buffer_;
  std::size_t cursor_ = 0;  // byte index in [0, buffer_.size()]
};

}  // namespace llmcli
