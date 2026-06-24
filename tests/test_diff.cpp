#include "util/Diff.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using llmcli::unified_diff;

TEST_CASE("identical text produces no diff", "[diff]") {
  CHECK(unified_diff("a\nb\nc\n", "a\nb\nc\n").empty());
  CHECK(unified_diff("", "").empty());
}

TEST_CASE("a new file is all additions", "[diff]") {
  const std::string d = unified_diff("", "line1\nline2\n");
  CHECK(d == "+line1\n+line2\n");
}

TEST_CASE("deleting everything is all removals", "[diff]") {
  const std::string d = unified_diff("x\ny\n", "");
  CHECK(d == "-x\n-y\n");
}

TEST_CASE("a single changed line shows -/+ with surrounding context",
          "[diff]") {
  const std::string old_text = "a\nb\nc\n";
  const std::string new_text = "a\nB\nc\n";
  const std::string d = unified_diff(old_text, new_text);
  // Context lines kept (within 3), the change as -b then +B.
  CHECK(d == " a\n-b\n+B\n c\n");
}

TEST_CASE("distant changes are split into hunks with a @@ separator", "[diff]") {
  std::string old_text, new_text;
  for (int i = 0; i < 20; ++i) {
    old_text += "line" + std::to_string(i) + "\n";
    new_text += "line" + std::to_string(i) + "\n";
  }
  // Change line 0 and line 19 — far apart, so two hunks.
  old_text = "X\n" + old_text.substr(old_text.find('\n') + 1);
  new_text = "Y\n" + new_text.substr(new_text.find('\n') + 1);
  // Replace the trailing line19 differently on each side.
  auto repl_last = [](std::string s, const std::string& with) {
    const std::string token = "line19\n";
    return s.substr(0, s.rfind(token)) + with;
  };
  old_text = repl_last(old_text, "old_end\n");
  new_text = repl_last(new_text, "new_end\n");

  const std::string d = unified_diff(old_text, new_text);
  CHECK(d.find("-X") != std::string::npos);
  CHECK(d.find("+Y") != std::string::npos);
  CHECK(d.find("-old_end") != std::string::npos);
  CHECK(d.find("+new_end") != std::string::npos);
  CHECK(d.find("@@") != std::string::npos);            // hunk separator
  CHECK(d.find("line10") == std::string::npos);        // middle context elided
}

TEST_CASE("an oversized input returns a summary, not a full diff", "[diff]") {
  std::string big;
  for (int i = 0; i < 50; ++i) big += "x\n";
  const std::string d = unified_diff("", big, /*context=*/3, /*max_lines=*/10);
  CHECK(d.find("too large") != std::string::npos);
  CHECK(d.find("50 lines") != std::string::npos);
}
