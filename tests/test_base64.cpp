#include "util/Base64.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using llmcli::base64_decode;
using llmcli::base64_encode;

TEST_CASE("base64 matches known RFC 4648 vectors", "[base64]") {
  CHECK(base64_encode("") == "");
  CHECK(base64_encode("f") == "Zg==");
  CHECK(base64_encode("fo") == "Zm8=");
  CHECK(base64_encode("foo") == "Zm9v");
  CHECK(base64_encode("foob") == "Zm9vYg==");
  CHECK(base64_encode("fooba") == "Zm9vYmE=");
  CHECK(base64_encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64 round-trips arbitrary binary, including high bytes",
          "[base64]") {
  std::string bytes;
  for (int i = 0; i < 256; ++i) bytes += static_cast<char>(i);
  bytes += bytes;  // 512 bytes, exercises every padding remainder
  CHECK(base64_decode(base64_encode(bytes)) == bytes);
}

TEST_CASE("decode skips whitespace in wrapped base64", "[base64]") {
  CHECK(base64_decode("Zm9v\nYmFy") == "foobar");
  CHECK(base64_decode("Zm9v Ymar") != "");  // tolerant, doesn't throw
}

TEST_CASE("decode stops at the first invalid character", "[base64]") {
  // "Zm9v" -> "foo"; the '!' is not a base64 digit, so decoding stops there.
  CHECK(base64_decode("Zm9v!!!!") == "foo");
}
