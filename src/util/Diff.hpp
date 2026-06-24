#pragma once

#include <string>

namespace llmcli {

// A line-based unified diff of `old_text` vs `new_text` for the edit-confirm
// preview. Lines are prefixed ' ' (context), '-' (removed), '+' (added), with a
// "@@" line between non-adjacent hunks; context is trimmed to `context` lines
// around each change. Returns "" when the texts are identical. The diff is
// O(n·m), so inputs over `max_lines` lines skip it and return a short summary.
std::string unified_diff(const std::string& old_text, const std::string& new_text,
                         int context = 3, int max_lines = 4000);

}  // namespace llmcli
