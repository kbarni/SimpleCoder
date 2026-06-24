#include "util/Diff.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace llmcli {

namespace {

// Split on '\n'; a trailing newline doesn't add an empty final line.
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

// One diff op against the line sequences.
struct Op {
  enum Kind { Equal, Del, Add } kind;
  std::string text;
};

// Longest-common-subsequence backtrack into a flat op list.
std::vector<Op> lcs_ops(const std::vector<std::string>& a,
                        const std::vector<std::string>& b) {
  const std::size_t n = a.size(), m = b.size();
  // dp[i][j] = LCS length of a[i..] and b[j..].
  std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
  for (std::size_t i = n; i-- > 0;) {
    for (std::size_t j = m; j-- > 0;) {
      dp[i][j] = (a[i] == b[j]) ? dp[i + 1][j + 1] + 1
                                : std::max(dp[i + 1][j], dp[i][j + 1]);
    }
  }

  std::vector<Op> ops;
  std::size_t i = 0, j = 0;
  while (i < n && j < m) {
    if (a[i] == b[j]) {
      ops.push_back({Op::Equal, a[i]});
      ++i;
      ++j;
    } else if (dp[i + 1][j] >= dp[i][j + 1]) {
      ops.push_back({Op::Del, a[i++]});
    } else {
      ops.push_back({Op::Add, b[j++]});
    }
  }
  for (; i < n; ++i) ops.push_back({Op::Del, a[i]});
  for (; j < m; ++j) ops.push_back({Op::Add, b[j]});
  return ops;
}

}  // namespace

std::string unified_diff(const std::string& old_text, const std::string& new_text,
                         int context, int max_lines) {
  if (old_text == new_text) return "";

  const std::vector<std::string> a = split_lines(old_text);
  const std::vector<std::string> b = split_lines(new_text);

  if (static_cast<int>(a.size()) > max_lines ||
      static_cast<int>(b.size()) > max_lines) {
    return "(" + std::to_string(a.size()) + " lines → " +
           std::to_string(b.size()) + " lines; diff too large to preview)";
  }

  const std::vector<Op> ops = lcs_ops(a, b);

  // Keep context lines within `context` of a change; elide the rest.
  const int n = static_cast<int>(ops.size());
  std::vector<bool> keep(n, false);
  for (int i = 0; i < n; ++i) {
    if (ops[i].kind == Op::Equal) continue;
    for (int k = std::max(0, i - context);
         k <= std::min(n - 1, i + context); ++k)
      keep[k] = true;
  }

  std::string out;
  bool in_gap = false;  // did we just elide some context?
  bool wrote_any = false;
  for (int i = 0; i < n; ++i) {
    if (ops[i].kind == Op::Equal && !keep[i]) {
      in_gap = true;
      continue;
    }
    if (in_gap && wrote_any) out += "@@\n";  // hunk separator
    in_gap = false;
    switch (ops[i].kind) {
      case Op::Equal: out += ' '; break;
      case Op::Del:   out += '-'; break;
      case Op::Add:   out += '+'; break;
    }
    out += ops[i].text;
    out += '\n';
    wrote_any = true;
  }
  return out;
}

}  // namespace llmcli
