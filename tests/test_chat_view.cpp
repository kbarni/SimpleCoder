#include "ui/ChatView.hpp"
#include "ui/InputBar.hpp"
#include "ui/TextWrap.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using llmcli::ChatView;
using llmcli::InputBar;
using llmcli::wrap_text;
using Lines = std::vector<std::string>;

// --- wrap_text -------------------------------------------------------------

TEST_CASE("short text fits on one line", "[wrap]") {
  CHECK(wrap_text("hello world", 20) == Lines{"hello world"});
}

TEST_CASE("greedy word wrap at width", "[wrap]") {
  CHECK(wrap_text("the quick brown fox", 9) ==
        Lines{"the quick", "brown fox"});
}

TEST_CASE("explicit newlines start new lines and blanks are kept", "[wrap]") {
  CHECK(wrap_text("a\n\nb", 10) == Lines{"a", "", "b"});
}

TEST_CASE("a word longer than the width is hard-broken", "[wrap]") {
  CHECK(wrap_text("abcdefgh", 3) == Lines{"abc", "def", "gh"});
}

TEST_CASE("long word mixed with short words", "[wrap]") {
  // "hi" then a 6-wide word that must hard-break.
  CHECK(wrap_text("hi abcdefg", 4) == Lines{"hi", "abcd", "efg"});
}

TEST_CASE("wrapping counts code points, not bytes", "[wrap][utf8]") {
  // Five accented letters, each 2 bytes (10 bytes) but 5 columns wide.
  const std::string s = "ààààà";
  // At width 5 it fits on one line; bytes-based wrapping would have split it.
  CHECK(wrap_text(s, 5) == Lines{"ààààà"});
  // At width 3 it splits on character boundaries: "ààà" then "àà".
  CHECK(wrap_text(s, 3) == Lines{"ààà", "àà"});
}

TEST_CASE("width below 1 is treated as 1", "[wrap]") {
  CHECK(wrap_text("ab", 0) == Lines{"a", "b"});
}

// --- ChatView::layout ------------------------------------------------------

TEST_CASE("layout prefixes speaker and separates entries with a blank line",
          "[chatview]") {
  ChatView v;
  // The caller supplies the full prefix verbatim ("> " for the user, "* " for
  // the assistant); ChatView does not append a separator.
  v.add("> ", "hello");
  v.add("* ", "hi there");

  CHECK(v.layout(40) == Lines{"> hello", "", "* hi there"});
}

TEST_CASE("layout wraps a long entry across lines", "[chatview]") {
  ChatView v;
  v.add("> ", "alpha beta gamma");  // "> alpha beta gamma"
  // width 11 -> "> alpha" (7) then "beta gamma" (10)
  CHECK(v.layout(11) == Lines{"> alpha", "beta gamma"});
}

TEST_CASE("speakerless entry has no prefix", "[chatview]") {
  ChatView v;
  v.add("", "system note");
  CHECK(v.layout(40) == Lines{"system note"});
}

TEST_CASE("preformatted entry preserves significant whitespace", "[chatview]") {
  ChatView v;
  v.add_preformatted("/   /\n\\   \\");  // ASCII-art-style runs of spaces
  // No word-wrap collapsing and no per-line markdown: lines come out verbatim.
  CHECK(v.layout(40) == Lines{"/   /", "\\   \\"});
}

TEST_CASE("preformatted entry is not word-wrapped", "[chatview]") {
  ChatView v;
  v.add_preformatted("aaaa bbbb cccc");
  // A normal entry would wrap at width 6; a preformatted one stays one line
  // (clipping to the width happens only at draw time).
  CHECK(v.layout(6) == Lines{"aaaa bbbb cccc"});
}

TEST_CASE("append_delta grows the last entry", "[chatview]") {
  ChatView v;
  v.add("* ", "");
  v.append_delta("Hel");
  v.append_delta("lo");
  CHECK(v.layout(40) == Lines{"* Hello"});
}

// --- ChatView collapsible thinking -----------------------------------------

TEST_CASE("an expanded thinking block shows a header and its body",
          "[chatview][thinking]") {
  ChatView v;
  std::size_t t = v.add_thinking();
  v.append_to(t, "step one\nstep two");
  CHECK(v.layout(40) ==
        Lines{"▾ thinking", "step one", "step two"});
}

TEST_CASE("a collapsed thinking block is a one-line summary",
          "[chatview][thinking]") {
  ChatView v;
  std::size_t t = v.add_thinking();
  v.append_to(t, "step one\nstep two\nstep three");
  v.set_collapsed(t, true);
  CHECK(v.layout(40) == Lines{"▸ thinking (3 lines)"});
}

TEST_CASE("toggle flips the most recent collapsible block",
          "[chatview][thinking]") {
  ChatView v;
  std::size_t t = v.add_thinking();
  v.append_to(t, "reasoning");

  v.toggle_last_collapsible();  // collapse
  CHECK(v.layout(40) == Lines{"▸ thinking (1 line)"});
  v.toggle_last_collapsible();  // expand
  CHECK(v.layout(40) == Lines{"▾ thinking", "reasoning"});
}

TEST_CASE("set_collapsed only affects collapsible entries",
          "[chatview][thinking]") {
  ChatView v;
  std::size_t a = v.add("* ", "hello");
  v.set_collapsed(a, true);  // no-op on a Normal entry
  CHECK(v.layout(40) == Lines{"* hello"});
}

// --- ChatView collapsible tool output --------------------------------------

TEST_CASE("a collapsed tool block summarizes with its header",
          "[chatview][tool]") {
  ChatView v;
  v.add_tool_output("read_file ✓", "line one\nline two");  // collapsed default
  CHECK(v.layout(40) == Lines{"▸ read_file ✓ (2 lines)"});
}

TEST_CASE("an expanded tool block shows the header and full body",
          "[chatview][tool]") {
  ChatView v;
  v.add_tool_output("grep_search ✓", "a\nb\nc", /*collapsed=*/false);
  CHECK(v.layout(40) == Lines{"▾ grep_search ✓", "a", "b", "c"});
}

TEST_CASE("toggle expands a collapsed tool block", "[chatview][tool]") {
  ChatView v;
  v.add_tool_output("run_bash ✗", "boom");
  CHECK(v.layout(40) == Lines{"▸ run_bash ✗ (1 line)"});
  v.toggle_last_collapsible();
  CHECK(v.layout(40) == Lines{"▾ run_bash ✗", "boom"});
}

// --- ChatView scrolling ----------------------------------------------------

namespace {
// A view with `n` single-line entries; at width 40 each entry is one display
// line and entries are separated by a blank line, so total = 2*n - 1 lines.
ChatView make_lines(int n) {
  ChatView v;
  for (int i = 0; i < n; ++i) v.add("", "line" + std::to_string(i));
  return v;
}
}  // namespace

TEST_CASE("new content pins the view to the bottom", "[chatview][scroll]") {
  ChatView v = make_lines(10);  // 19 display lines
  const int width = 40, rows = 5;
  // Bottom-pinned: first visible line is total - rows.
  CHECK(v.first_visible_line(width, rows) == 19 - rows);
}

TEST_CASE("scroll up/down moves by lines and clamps at both ends",
          "[chatview][scroll]") {
  ChatView v = make_lines(10);  // 19 lines
  const int width = 40, rows = 5;

  v.scroll_up(3);
  CHECK(v.first_visible_line(width, rows) == 19 - rows - 3);

  v.scroll_down(1);
  CHECK(v.first_visible_line(width, rows) == 19 - rows - 2);

  // Over-scrolling past the top clamps to the first line, and the offset is
  // clamped (so a single scroll_down steps back off the top, not 1000 times).
  v.scroll_up(1000);
  CHECK(v.first_visible_line(width, rows) == 0);
  v.scroll_down(1);
  CHECK(v.first_visible_line(width, rows) == 1);

  // Over-scrolling past the bottom clamps to the bottom.
  v.scroll_down(1000);
  CHECK(v.first_visible_line(width, rows) == 19 - rows);
}

TEST_CASE("home/end jump to the top and bottom", "[chatview][scroll]") {
  ChatView v = make_lines(10);
  const int width = 40, rows = 5;

  v.scroll_to_top();
  CHECK(v.first_visible_line(width, rows) == 0);

  v.scroll_to_bottom();
  CHECK(v.first_visible_line(width, rows) == 19 - rows);
}

TEST_CASE("paging moves by a screenful with one line of overlap",
          "[chatview][scroll]") {
  ChatView v = make_lines(20);  // 39 lines
  const int width = 40, rows = 10;

  v.scroll_page_up(rows);  // up by rows-1 = 9
  CHECK(v.first_visible_line(width, rows) == 39 - rows - 9);

  v.scroll_page_down(rows);  // back down by 9
  CHECK(v.first_visible_line(width, rows) == 39 - rows);
}

TEST_CASE("content shorter than the viewport stays at the top",
          "[chatview][scroll]") {
  ChatView v = make_lines(2);  // 3 lines, viewport taller
  const int width = 40, rows = 20;
  CHECK(v.first_visible_line(width, rows) == 0);
  v.scroll_up(5);
  CHECK(v.first_visible_line(width, rows) == 0);
}

// --- InputBar key handling -------------------------------------------------

TEST_CASE("typing accumulates printable characters", "[inputbar]") {
  InputBar bar;
  for (char c : std::string("hi!")) {
    CHECK_FALSE(bar.handle_key(c).has_value());
  }
  CHECK(bar.buffer() == "hi!");
}

TEST_CASE("backspace removes the last character", "[inputbar]") {
  InputBar bar;
  bar.handle_key('a');
  bar.handle_key('b');
  bar.handle_key(127);  // DEL
  CHECK(bar.buffer() == "a");
  bar.handle_key(127);
  bar.handle_key(127);  // extra backspace on empty is a no-op
  CHECK(bar.buffer().empty());
}

TEST_CASE("enter submits the buffer and clears it", "[inputbar]") {
  InputBar bar;
  for (char c : std::string("send me")) bar.handle_key(c);
  auto submitted = bar.handle_key('\n');
  REQUIRE(submitted.has_value());
  CHECK(*submitted == "send me");
  CHECK(bar.buffer().empty());
}

TEST_CASE("control keys are ignored", "[inputbar]") {
  InputBar bar;
  bar.handle_key('\t');  // tab ignored
  bar.handle_key(1);     // ^A ignored
  CHECK(bar.buffer().empty());
}
