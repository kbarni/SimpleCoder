#include <catch2/catch_test_macros.hpp>

#include <string>

#include "util/Utf8.hpp"

using namespace llmcli;

TEST_CASE("encode produces the expected UTF-8 bytes", "[utf8]") {
  CHECK(utf8::encode(U'A') == "A");          // 1 byte
  CHECK(utf8::encode(U'é') == "\xC3\xA9");   // 2 bytes
  CHECK(utf8::encode(U'€') == "\xE2\x82\xAC");  // 3 bytes
  CHECK(utf8::encode(U'😀') == "\xF0\x9F\x98\x80");  // 4 bytes
}

TEST_CASE("count measures code points, not bytes", "[utf8]") {
  CHECK(utf8::count("abc") == 3);
  CHECK(utf8::count("café") == 4);   // é is 2 bytes
  CHECK(utf8::count("€10") == 3);    // € is 3 bytes
  CHECK(utf8::count("") == 0);
}

TEST_CASE("boundaries step over whole characters", "[utf8]") {
  const std::string s = "aé";  // 'a' (1 byte) + 'é' (2 bytes) => size 3
  REQUIRE(s.size() == 3);

  CHECK(utf8::next_boundary(s, 0) == 1);  // past 'a'
  CHECK(utf8::next_boundary(s, 1) == 3);  // past 'é'
  CHECK(utf8::next_boundary(s, 3) == 3);  // clamped at end

  CHECK(utf8::prev_boundary(s, 3) == 1);  // back over 'é'
  CHECK(utf8::prev_boundary(s, 1) == 0);  // back over 'a'
  CHECK(utf8::prev_boundary(s, 0) == 0);  // clamped at start
}

TEST_CASE("snap_boundary moves off a continuation byte", "[utf8]") {
  const std::string s = "é";  // 2 bytes
  CHECK(utf8::snap_boundary(s, 0) == 0);
  CHECK(utf8::snap_boundary(s, 1) == 0);  // mid-character -> start
  CHECK(utf8::snap_boundary(s, 2) == 2);
}
