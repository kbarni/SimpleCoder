#include <catch2/catch_test_macros.hpp>

#include "ui/Markdown.hpp"

using namespace llmcli;

TEST_CASE("plain prose is Text", "[markdown]") {
  auto md = markdown_lines("just a sentence");
  REQUIRE(md.size() == 1);
  CHECK(md[0].kind == MdKind::Text);
  CHECK(md[0].text == "just a sentence");
}

TEST_CASE("ATX headers are detected and markers stripped", "[markdown]") {
  auto md = markdown_lines("## Title here");
  REQUIRE(md.size() == 1);
  CHECK(md[0].kind == MdKind::Header);
  CHECK(md[0].text == "Title here");

  // A '#' without a following space is not a header.
  auto h = markdown_lines("#nospace");
  CHECK(h[0].kind == MdKind::Text);
}

TEST_CASE("fenced code blocks drop the fences and style the body", "[markdown]") {
  auto md = markdown_lines("before\n```\ncode 1\ncode 2\n```\nafter");
  REQUIRE(md.size() == 4);
  CHECK(md[0].kind == MdKind::Text);   // before
  CHECK(md[1].kind == MdKind::Code);   // code 1
  CHECK(md[1].text == "code 1");
  CHECK(md[2].kind == MdKind::Code);   // code 2
  CHECK(md[3].kind == MdKind::Text);   // after
}

TEST_CASE("an unterminated fence runs to the end", "[markdown]") {
  auto md = markdown_lines("```python\nx = 1\ny = 2");
  REQUIRE(md.size() == 2);  // the opening fence is dropped
  CHECK(md[0].kind == MdKind::Code);
  CHECK(md[1].kind == MdKind::Code);
}

TEST_CASE("a fence with a language label is still hidden", "[markdown]") {
  auto md = markdown_lines("```cpp\nint main();\n```");
  REQUIRE(md.size() == 1);
  CHECK(md[0].kind == MdKind::Code);
  CHECK(md[0].text == "int main();");
}

TEST_CASE("bullet lists normalise the marker", "[markdown]") {
  auto md = markdown_lines("- first\n* second\n+ third");
  REQUIRE(md.size() == 3);
  for (const auto& l : md) CHECK(l.kind == MdKind::ListItem);
  CHECK(md[0].text == "• first");
  CHECK(md[1].text == "• second");
  CHECK(md[2].text == "• third");
}

TEST_CASE("numbered lists are list items", "[markdown]") {
  auto md = markdown_lines("1. one\n2. two");
  REQUIRE(md.size() == 2);
  CHECK(md[0].kind == MdKind::ListItem);
  CHECK(md[0].text == "1. one");
}

TEST_CASE("a hyphen mid-sentence is not a list", "[markdown]") {
  auto md = markdown_lines("well-formed text");
  CHECK(md[0].kind == MdKind::Text);
}
