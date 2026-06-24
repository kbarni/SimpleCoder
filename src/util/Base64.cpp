#include "util/Base64.hpp"

#include <array>
#include <cctype>
#include <cstdint>

namespace llmcli {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Reverse lookup: ASCII char -> 6-bit value, or 0xFF if not a base64 digit.
std::array<std::uint8_t, 256> make_reverse() {
  std::array<std::uint8_t, 256> t{};
  t.fill(0xFF);
  for (std::uint8_t i = 0; i < 64; ++i)
    t[static_cast<std::uint8_t>(kAlphabet[i])] = i;
  return t;
}

}  // namespace

std::string base64_encode(std::string_view bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t i = 0;
  for (; i + 3 <= bytes.size(); i += 3) {
    const auto b0 = static_cast<std::uint8_t>(bytes[i]);
    const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
    const auto b2 = static_cast<std::uint8_t>(bytes[i + 2]);
    out += kAlphabet[b0 >> 2];
    out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
    out += kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)];
    out += kAlphabet[b2 & 0x3F];
  }

  if (const std::size_t rem = bytes.size() - i; rem == 1) {
    const auto b0 = static_cast<std::uint8_t>(bytes[i]);
    out += kAlphabet[b0 >> 2];
    out += kAlphabet[(b0 & 0x03) << 4];
    out += "==";
  } else if (rem == 2) {
    const auto b0 = static_cast<std::uint8_t>(bytes[i]);
    const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
    out += kAlphabet[b0 >> 2];
    out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
    out += kAlphabet[(b1 & 0x0F) << 2];
    out += '=';
  }
  return out;
}

std::string base64_decode(std::string_view b64) {
  static const std::array<std::uint8_t, 256> rev = make_reverse();
  std::string out;
  out.reserve(b64.size() / 4 * 3);

  std::uint32_t buf = 0;
  int bits = 0;
  for (char c : b64) {
    if (c == '=') break;  // padding: nothing more to decode
    const auto uc = static_cast<std::uint8_t>(c);
    if (std::isspace(uc)) continue;  // tolerate line breaks in wrapped base64
    const std::uint8_t v = rev[uc];
    if (v == 0xFF) break;  // invalid char: stop (best-effort)
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xFF);
    }
  }
  return out;
}

}  // namespace llmcli
