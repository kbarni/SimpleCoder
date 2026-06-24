#include "ui/ChatView.hpp"

#include <algorithm>
#include <climits>

#include "ui/Markdown.hpp"
#include "ui/TextWrap.hpp"
#include "ui/Theme.hpp"

namespace llmcli {

std::size_t ChatView::add(std::string speaker, std::string text, Style style) {
  entries_.push_back({std::move(speaker), std::move(text), style});
  scroll_to_bottom();
  return entries_.size() - 1;
}

void ChatView::append_to(std::size_t index, std::string_view text) {
  if (index >= entries_.size()) return;
  entries_[index].text.append(text);
  scroll_to_bottom();
}

void ChatView::append_delta(std::string_view text) {
  if (entries_.empty()) entries_.push_back({"", "", Style::Normal});
  entries_.back().text.append(text);
  scroll_to_bottom();
}

void ChatView::clear() {
  entries_.clear();
  scroll_from_bottom_ = 0;
}

std::size_t ChatView::add_preformatted(std::string text, Style style) {
  entries_.push_back({"", std::move(text), style, Kind::Preformatted, false});
  scroll_to_bottom();
  return entries_.size() - 1;
}

std::size_t ChatView::add_thinking() {
  entries_.push_back({"thinking", "", Style::Dim, Kind::Thinking, false});
  scroll_to_bottom();
  return entries_.size() - 1;
}

std::size_t ChatView::add_tool_output(std::string header, std::string body,
                                      bool collapsed) {
  entries_.push_back({std::move(header), std::move(body), Style::Dim,
                      Kind::Tool, collapsed});
  scroll_to_bottom();
  return entries_.size() - 1;
}

void ChatView::set_collapsed(std::size_t index, bool collapsed) {
  if (index >= entries_.size()) return;
  const Kind k = entries_[index].kind;
  if (k != Kind::Thinking && k != Kind::Tool) return;
  entries_[index].collapsed = collapsed;
}

void ChatView::toggle_last_collapsible() {
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (it->kind == Kind::Thinking || it->kind == Kind::Tool) {
      it->collapsed = !it->collapsed;
      return;
    }
  }
}

std::vector<ChatView::DisplayLine> ChatView::layout_styled(int width) const {
  std::vector<DisplayLine> lines;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (i != 0) lines.push_back({"", Style::Normal});  // blank separator
    const Entry& e = entries_[i];

    if (e.kind == Kind::Thinking || e.kind == Kind::Tool) {
      // Thinking blocks use a fixed "thinking" label; tool blocks carry their
      // summary label in `speaker` (e.g. "read_file ✓").
      const std::string label =
          e.kind == Kind::Thinking ? "thinking" : e.speaker;
      const auto body = wrap_text(e.text, width);
      if (e.collapsed) {
        const std::size_t n = e.text.empty() ? 0 : body.size();
        lines.push_back({"▸ " + label + " (" + std::to_string(n) +
                             (n == 1 ? " line)" : " lines)"),
                         e.style});
      } else {
        lines.push_back({"▾ " + label, e.style});
        for (auto& wrapped : body) lines.push_back({std::move(wrapped), e.style});
      }
      continue;
    }

    if (e.kind == Kind::Preformatted) {
      // Verbatim: emit each source line as-is, skipping markdown and word-wrap
      // (which collapse significant whitespace). Over-long lines are clipped at
      // draw time by mvwaddnstr.
      std::size_t start = 0;
      while (true) {
        const std::size_t nl = e.text.find('\n', start);
        lines.push_back(
            {e.text.substr(start, nl == std::string::npos ? std::string::npos
                                                          : nl - start),
             e.style});
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
      continue;
    }

    // Block-level markdown: classify each source line and style it, keeping the
    // speaker label on the first line. The caller supplies the full prefix it
    // wants (e.g. "> " for the user, "* " for the assistant), so it is prepended
    // verbatim — no separator is added here.
    const std::string prefix = e.speaker;
    const std::vector<MdLine> md = markdown_lines(e.text);
    for (std::size_t k = 0; k < md.size(); ++k) {
      const MdLine& ml = md[k];
      const std::string content = (k == 0 ? prefix + ml.text : ml.text);

      Style st = e.style;
      if (e.style == Style::Normal) {
        if (ml.kind == MdKind::Header)
          st = Style::Accent;
        else if (ml.kind == MdKind::Code)
          st = Style::Dim;
      }

      const std::vector<std::string> wrapped = wrap_text(content, width);
      for (std::size_t j = 0; j < wrapped.size(); ++j) {
        // List items hang-indent their wrapped continuation under the text.
        const std::string line =
            (ml.kind == MdKind::ListItem && j > 0) ? "  " + wrapped[j]
                                                   : wrapped[j];
        lines.push_back({line, st});
      }
    }
  }
  return lines;
}

std::vector<std::string> ChatView::layout(int width) const {
  std::vector<std::string> lines;
  for (const auto& dl : layout_styled(width)) lines.push_back(dl.text);
  return lines;
}

void ChatView::scroll_up(int lines) {
  scroll_from_bottom_ += std::max(0, lines);
}

void ChatView::scroll_down(int lines) {
  scroll_from_bottom_ = std::max(0, scroll_from_bottom_ - std::max(0, lines));
}

void ChatView::scroll_page_up(int viewport_rows) {
  scroll_up(std::max(1, viewport_rows - 1));
}

void ChatView::scroll_page_down(int viewport_rows) {
  scroll_down(std::max(1, viewport_rows - 1));
}

void ChatView::scroll_to_top() {
  // A large value; clamped to the real maximum on the next render/query.
  scroll_from_bottom_ = INT_MAX / 2;
}

int ChatView::first_visible_line(int width, int viewport_rows) const {
  const int total = static_cast<int>(layout_styled(width).size());
  const int max_offset = std::max(0, total - viewport_rows);
  scroll_from_bottom_ = std::clamp(scroll_from_bottom_, 0, max_offset);
  return std::max(0, total - viewport_rows - scroll_from_bottom_);
}

void ChatView::render(WINDOW* win) const {
  int height = 0, width = 0;
  getmaxyx(win, height, width);
  werase(win);
  if (height <= 0 || width <= 0) {
    wnoutrefresh(win);
    return;
  }

  const std::vector<DisplayLine> lines = layout_styled(width);
  const int total = static_cast<int>(lines.size());

  // Clamp the scroll offset to the content and pick the first visible line
  // (pinned near the bottom by default).
  const int first = first_visible_line(width, height);

  int row = 0;
  for (int i = first; i < total && row < height; ++i, ++row) {
    const auto attr = [&]() -> attr_t {
      switch (lines[i].style) {
        case Style::Dim:
          return theme::dim_attr();
        case Style::Error:
          return theme::error_attr();
        case Style::Accent:
          return theme::accent_attr();
        case Style::Normal:
          break;
      }
      return A_NORMAL;
    }();
    wattron(win, attr);
    mvwaddnstr(win, row, 0, lines[i].text.c_str(), width);
    wattroff(win, attr);
  }
  wnoutrefresh(win);
}

}  // namespace llmcli
