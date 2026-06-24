#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ui/InputBar.hpp"

using namespace llmcli;

namespace {
void type(InputBar& bar, const std::string& s) {
  for (char c : s) bar.handle_key(static_cast<unsigned char>(c));
}
}  // namespace

TEST_CASE("typing advances the cursor", "[inputbar]") {
  InputBar bar;
  type(bar, "hello");
  CHECK(bar.buffer() == "hello");
  CHECK(bar.cursor() == 5);
}

TEST_CASE("left/right move the cursor and insert happens there", "[inputbar]") {
  InputBar bar;
  type(bar, "helo");
  bar.handle_key(KEY_LEFT);  // between 'l' and 'o'
  bar.handle_key('l');       // -> "hello"
  CHECK(bar.buffer() == "hello");
  CHECK(bar.cursor() == 4);

  // Cursor clamps at the right end.
  bar.handle_key(KEY_RIGHT);
  bar.handle_key(KEY_RIGHT);
  CHECK(bar.cursor() == 5);
}

TEST_CASE("backspace deletes before the cursor, not at the end", "[inputbar]") {
  InputBar bar;
  type(bar, "abc");
  bar.handle_key(KEY_LEFT);     // cursor between 'b' and 'c'
  bar.handle_key(KEY_BACKSPACE);  // deletes 'b'
  CHECK(bar.buffer() == "ac");
  CHECK(bar.cursor() == 1);
}

TEST_CASE("alt-enter inserts a newline without submitting", "[inputbar]") {
  InputBar bar;
  type(bar, "line1");
  bar.insert_newline();
  type(bar, "line2");
  CHECK(bar.buffer() == "line1\nline2");
  CHECK(bar.line_count() == 2);
}

TEST_CASE("enter submits the whole multi-line buffer and clears", "[inputbar]") {
  InputBar bar;
  type(bar, "a");
  bar.insert_newline();
  type(bar, "b");
  auto submitted = bar.handle_key('\n');
  REQUIRE(submitted.has_value());
  CHECK(*submitted == "a\nb");
  CHECK(bar.buffer().empty());
  CHECK(bar.cursor() == 0);
  CHECK(bar.line_count() == 1);
}

TEST_CASE("home/end move within the current line", "[inputbar]") {
  InputBar bar;
  type(bar, "one");
  bar.insert_newline();
  type(bar, "two");  // cursor at end of "two"
  bar.handle_key(KEY_HOME);
  CHECK(bar.cursor() == 4);  // start of "two" (after "one\n")
  bar.handle_key(KEY_END);
  CHECK(bar.cursor() == 7);  // end of buffer
}

TEST_CASE("up/down move between lines keeping the column", "[inputbar]") {
  InputBar bar;
  type(bar, "abcd");
  bar.insert_newline();
  type(bar, "xy");  // line 2 is shorter; cursor at end (col 2)
  bar.handle_key(KEY_UP);
  // Column 2 on the first line is between 'b' and 'c' -> index 2.
  CHECK(bar.cursor() == 2);
  bar.handle_key(KEY_DOWN);
  // Back to line 2, column 2 -> end of "xy" = index 7.
  CHECK(bar.cursor() == 7);
}

TEST_CASE("up on the first line is a no-op", "[inputbar]") {
  InputBar bar;
  type(bar, "abc");
  bar.handle_key(KEY_LEFT);  // cursor at index 2
  bar.handle_key(KEY_UP);
  CHECK(bar.cursor() == 2);
}

TEST_CASE("insert_text adds multibyte characters", "[inputbar][utf8]") {
  InputBar bar;
  type(bar, "caf");
  bar.insert_text("é");  // 2 bytes
  CHECK(bar.buffer() == "café");
  CHECK(bar.cursor() == 5);  // 3 + 2 bytes
}

TEST_CASE("cursor and backspace step over whole multibyte characters",
          "[inputbar][utf8]") {
  InputBar bar;
  bar.insert_text("é");  // 2 bytes, cursor at 2
  bar.insert_text("a");  // "éa", cursor at 3

  bar.handle_key(KEY_LEFT);  // over 'a'
  CHECK(bar.cursor() == 2);
  bar.handle_key(KEY_LEFT);  // over 'é' (whole char, not one byte)
  CHECK(bar.cursor() == 0);

  bar.handle_key(KEY_END);
  bar.handle_key(KEY_BACKSPACE);  // delete 'a'
  bar.handle_key(KEY_BACKSPACE);  // delete the whole 'é'
  CHECK(bar.buffer().empty());
}

// --- soft wrapping ---------------------------------------------------------

TEST_CASE("a long line soft-wraps into extra visual rows", "[inputbar][wrap]") {
  InputBar bar;
  type(bar, "abcdefghij");  // 10 chars, one logical line
  // width 6, no prompt, 1 reserved column -> 5 columns/row -> 2 rows.
  CHECK(bar.line_count() == 1);          // logical count unchanged
  CHECK(bar.visual_line_count(6, 0) == 2);
}

TEST_CASE("the prompt narrows only the first visual row", "[inputbar][wrap]") {
  InputBar bar;
  type(bar, "abcdef");  // 6 chars
  // width 6, prompt 2 cols, reserve 1 -> row0 budget 3, later rows budget 5.
  const auto wl = bar.wrap_layout(6, 2);
  REQUIRE(wl.rows.size() == 2);
  CHECK(wl.rows[0] == "abc");
  CHECK(wl.rows[1] == "def");
}

TEST_CASE("newlines and soft wraps both add rows", "[inputbar][wrap]") {
  InputBar bar;
  type(bar, "aaaa");
  bar.insert_newline();
  type(bar, "bbbbbbbb");  // second logical line, 8 chars
  // width 6, no prompt, budget 5: line1 -> 1 row, line2 -> 2 rows = 3.
  CHECK(bar.line_count() == 2);
  CHECK(bar.visual_line_count(6, 0) == 3);
}

TEST_CASE("cursor follows the soft wrap onto the next row", "[inputbar][wrap]") {
  InputBar bar;
  type(bar, "abcdef");  // cursor at end (byte 6)
  // width 6, no prompt, budget 5: rows "abcde","f"; cursor at end -> row1 col1.
  const auto wl = bar.wrap_layout(6, 0);
  REQUIRE(wl.rows.size() == 2);
  CHECK(wl.cursor_row == 1);
  CHECK(wl.cursor_col == 1);
}

TEST_CASE("a short line stays a single visual row", "[inputbar][wrap]") {
  InputBar bar;
  type(bar, "hi");
  CHECK(bar.visual_line_count(40, 2) == 1);
}
