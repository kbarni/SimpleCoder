#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <ncursesw/ncurses.h>

namespace llmcli {

// Scrollable conversation panel. The conversation is stored as labelled
// entries; turning them into wrapped display lines (`layout`) is pure logic and
// unit-tested, while `render` paints those lines into an ncurses window.
class ChatView {
 public:
  // Visual treatment for an entry's lines.
  enum class Style { Normal, Dim, Error, Accent };

  // Normal entries render their text; Thinking and Tool entries can be
  // collapsed to a one-line summary (reasoning, and tool output, respectively);
  // Preformatted entries are emitted verbatim (no markdown, no word-wrap) so
  // significant whitespace — e.g. the ASCII-art banner — is preserved.
  enum class Kind { Normal, Thinking, Preformatted, Tool };

  struct Entry {
    std::string speaker;  // e.g. "you", "assistant"; empty for system notes
    std::string text;
    Style style = Style::Normal;
    Kind kind = Kind::Normal;
    bool collapsed = false;
  };

  struct DisplayLine {
    std::string text;
    Style style = Style::Normal;
  };

  // Start a new entry; returns its index for later append_to().
  std::size_t add(std::string speaker, std::string text,
                  Style style = Style::Normal);
  // Add a verbatim entry: each source line is shown as-is (clipped to the
  // window width), bypassing markdown and word-wrap. For ASCII art / banners.
  std::size_t add_preformatted(std::string text, Style style = Style::Normal);
  // Append streamed text to a specific entry.
  void append_to(std::size_t index, std::string_view text);
  // Append streamed text to the most recent entry (creating one if needed).
  void append_delta(std::string_view text);
  void clear();

  // Create a collapsible reasoning ("thinking") entry, initially expanded.
  std::size_t add_thinking();
  // Add a collapsible tool-output entry: `header` is the one-line summary label
  // (e.g. "read_file ✓"), `body` the full output. Collapsed by default.
  std::size_t add_tool_output(std::string header, std::string body,
                              bool collapsed = true);
  // Collapse/expand a specific entry (no-op unless it is Thinking or Tool).
  void set_collapsed(std::size_t index, bool collapsed);
  // Toggle the most recent collapsible (Thinking or Tool) entry; no-op if none.
  void toggle_last_collapsible();

  const std::vector<Entry>& entries() const { return entries_; }

  // Pure: wrap all entries into styled display lines for the given column
  // width. Entries are separated by a blank line. No ncurses calls.
  std::vector<DisplayLine> layout_styled(int width) const;

  // Convenience: just the wrapped text, dropping styling.
  std::vector<std::string> layout(int width) const;

  // Scroll by whole lines. Positive = toward older content. The offset is
  // clamped to the content on the next render/first_visible_line, so
  // over-scrolling past either end is harmless.
  void scroll_up(int lines);
  void scroll_down(int lines);
  // Scroll by a screenful (viewport height in rows), leaving one line of
  // overlap for orientation.
  void scroll_page_up(int viewport_rows);
  void scroll_page_down(int viewport_rows);
  void scroll_to_top();  // oldest content (clamped on next render)
  void scroll_to_bottom() { scroll_from_bottom_ = 0; }

  // Pure: index of the first visible display line for the given geometry,
  // honouring (and clamping) the current scroll offset. Used by render and by
  // headless tests. No ncurses calls.
  int first_visible_line(int width, int viewport_rows) const;

  // Paint into `win`, showing the lines that fit, honouring the scroll offset.
  void render(WINDOW* win) const;

 private:
  std::vector<Entry> entries_;
  // Lines scrolled back from the newest; 0 = pinned to the bottom. Mutable so
  // it can be clamped to the content during the const render/query path.
  mutable int scroll_from_bottom_ = 0;
};

}  // namespace llmcli
