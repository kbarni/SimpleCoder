#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// Wrap `text` into display lines no wider than `width` columns.
//   - Explicit '\n' characters always start a new line (blank lines preserved).
//   - Wrapping happens at spaces (greedy word-fill).
//   - A single word longer than `width` is hard-broken into width-sized chunks.
//   - `width < 1` is treated as 1.
//
// This is pure logic with no ncurses dependency, so it can be unit-tested
// headless and reused by ChatView's renderer.
std::vector<std::string> wrap_text(std::string_view text, int width);

}  // namespace llmcli
