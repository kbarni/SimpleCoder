#include "ui/TextWrap.hpp"

#include "util/Utf8.hpp"

namespace llmcli {

namespace {

// Column width of a byte range, measured in code points (see Utf8.hpp).
std::size_t cols(std::string_view s) { return utf8::count(s); }

// First `n` code points of `word` as a byte substring.
std::string_view take_cols(std::string_view word, std::size_t n) {
  std::size_t i = 0;
  for (std::size_t k = 0; k < n && i < word.size(); ++k)
    i = utf8::next_boundary(word, i);
  return word.substr(0, i);
}

// Wrap one paragraph (no embedded newlines) and append its lines to `out`.
// Widths are counted in code points and breaks land on character boundaries,
// so multibyte text is never split mid-character.
void wrap_paragraph(std::string_view para, std::size_t width,
                    std::vector<std::string>& out) {
  if (para.empty()) {
    out.emplace_back();  // preserve blank line
    return;
  }

  std::string line;
  std::size_t line_cols = 0;
  std::size_t i = 0;
  while (i < para.size()) {
    // Skip runs of spaces between words.
    while (i < para.size() && para[i] == ' ') ++i;
    if (i >= para.size()) break;

    const std::size_t word_begin = i;
    while (i < para.size() && para[i] != ' ') ++i;
    std::string_view word = para.substr(word_begin, i - word_begin);

    // A word wider than the line is hard-broken on character boundaries.
    while (cols(word) > width) {
      if (!line.empty()) {
        out.push_back(line);
        line.clear();
        line_cols = 0;
      }
      std::string_view chunk = take_cols(word, width);
      out.emplace_back(chunk);
      word.remove_prefix(chunk.size());
    }

    const std::size_t word_cols = cols(word);
    if (line.empty()) {
      line = std::string(word);
      line_cols = word_cols;
    } else if (line_cols + 1 + word_cols <= width) {
      line += ' ';
      line += word;
      line_cols += 1 + word_cols;
    } else {
      out.push_back(line);
      line = std::string(word);
      line_cols = word_cols;
    }
  }

  if (!line.empty()) out.push_back(line);
}

}  // namespace

std::vector<std::string> wrap_text(std::string_view text, int width) {
  const std::size_t w = width < 1 ? 1 : static_cast<std::size_t>(width);
  std::vector<std::string> out;

  std::size_t start = 0;
  while (true) {
    const std::size_t nl = text.find('\n', start);
    const std::string_view para =
        text.substr(start, nl == std::string_view::npos ? std::string_view::npos
                                                         : nl - start);
    wrap_paragraph(para, w, out);
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  return out;
}

}  // namespace llmcli
