#include "util/Utf8.hpp"

namespace llmcli::utf8 {

int seq_len(unsigned char b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1;  // continuation byte or invalid lead: advance one byte
}

std::string encode(char32_t cp) {
  std::string out;
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

std::size_t prev_boundary(std::string_view s, std::size_t pos) {
  if (pos == 0) return 0;
  std::size_t i = pos - 1;
  while (i > 0 && is_cont(static_cast<unsigned char>(s[i]))) --i;
  return i;
}

std::size_t next_boundary(std::string_view s, std::size_t pos) {
  if (pos >= s.size()) return s.size();
  std::size_t i = pos + seq_len(static_cast<unsigned char>(s[pos]));
  return i > s.size() ? s.size() : i;
}

std::size_t snap_boundary(std::string_view s, std::size_t pos) {
  if (pos == 0 || pos >= s.size()) return pos > s.size() ? s.size() : pos;
  if (!is_cont(static_cast<unsigned char>(s[pos]))) return pos;
  std::size_t i = pos;
  while (i > 0 && is_cont(static_cast<unsigned char>(s[i]))) --i;
  return i;
}

std::size_t count(std::string_view s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size();
       i += seq_len(static_cast<unsigned char>(s[i])))
    ++n;
  return n;
}

}  // namespace llmcli::utf8
