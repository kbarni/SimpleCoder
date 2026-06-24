#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Minimal UTF-8 helpers for cursor movement and column-based wrapping. Width is
// counted in code points (one column each); this is correct for ASCII and
// accented Latin. Wide (CJK) and zero-width/combining characters are treated as
// one column — a known limitation, not handled here.
namespace llmcli::utf8 {

// Bytes in the sequence whose lead byte is `b` (1..4); 1 for a stray
// continuation/invalid byte so iteration always advances.
int seq_len(unsigned char b);

// True if byte `b` is a UTF-8 continuation byte (10xxxxxx).
inline bool is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

// Encode a Unicode scalar value to UTF-8.
std::string encode(char32_t cp);

// Byte index of the previous / next character boundary relative to `pos`.
std::size_t prev_boundary(std::string_view s, std::size_t pos);
std::size_t next_boundary(std::string_view s, std::size_t pos);

// Snap `pos` back to the start of the character it lands in (no-op if already
// on a boundary).
std::size_t snap_boundary(std::string_view s, std::size_t pos);

// Number of code points in `s` (its display width under the one-column model).
std::size_t count(std::string_view s);

}  // namespace llmcli::utf8
