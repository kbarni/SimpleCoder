#include "app/App.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "app/Attachments.hpp"
#include "app/Command.hpp"
#include "app/SystemPrompt.hpp"
#include "ui/Banner.hpp"
#include "ui/ConfirmDialog.hpp"
#include "ui/Theme.hpp"
#include "ui/Tui.hpp"
#include "util/Log.hpp"
#include "util/Utf8.hpp"

namespace llmcli {

namespace {
// Read a whole file for @-attachment inlining; nullopt if it can't be opened.
std::optional<std::string> read_file_opt(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// A short listing of the project's files, for /init context. Skips noise dirs
// and caps depth/count so the prompt stays bounded.
std::string project_tree(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  static const std::set<std::string> skip = {".git", "build", "node_modules",
                                              ".cache", "third_party"};
  std::vector<std::string> entries;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; it != end && entries.size() < 200; it.increment(ec)) {
    if (ec) break;
    const fs::path& p = it->path();
    const std::string name = p.filename().string();
    const bool is_dir = it->is_directory(ec);
    if (is_dir && (skip.count(name) || (name.size() > 1 && name[0] == '.'))) {
      it.disable_recursion_pending();
      continue;
    }
    if (it.depth() > 3) continue;
    if (!is_dir) entries.push_back(fs::relative(p, root, ec).string());
  }
  std::sort(entries.begin(), entries.end());
  std::string out;
  for (const auto& e : entries) out += e + "\n";
  return out;
}
}  // namespace

App::App(Config cfg)
    : agent_(std::move(cfg), [this](const std::string& tool,
                                    const std::string& details) {
        return request_confirmation(tool, details);
      }),
      skills_(discoverSkills()) {}

App::~App() { join_worker(); }

void App::join_worker() {
  if (worker_.joinable()) worker_.join();
}

ConfirmChoice App::request_confirmation(const std::string& tool,
                                        const std::string& details) {
  auto reply = std::make_shared<std::promise<ConfirmChoice>>();
  std::future<ConfirmChoice> fut = reply->get_future();
  events_.push({Event::Type::ConfirmRequest, details, tool, reply});
  return fut.get();  // blocks the worker until the UI thread answers
}

void App::rebuild_windows() {
  // Derived (inner) windows must be deleted before their parents.
  if (chat_inner_) delwin(chat_inner_);
  if (input_inner_) delwin(input_inner_);
  if (header_win_) delwin(header_win_);
  if (chat_outer_) delwin(chat_outer_);
  if (input_outer_) delwin(input_outer_);

  // The input box grows with the typed text up to a cap, then scrolls. A
  // single header row sits on top; the chat box fills the middle.
  constexpr int kMaxInputRows = 6;
  const int input_rows = std::clamp(
      input_.visual_line_count(std::max(1, COLS - 2), 2), 1, kMaxInputRows);
  input_lines_shown_ = input_rows;
  const int input_h = input_rows + 2;  // + top/bottom border
  const int chat_h = std::max(3, LINES - 1 - input_h);

  header_win_ = newwin(1, COLS, 0, 0);
  chat_outer_ = newwin(chat_h, COLS, 1, 0);
  input_outer_ = newwin(input_h, COLS, LINES - input_h, 0);

  // Content areas inset by the 1-cell border on each side.
  chat_inner_ = derwin(chat_outer_, std::max(1, chat_h - 2),
                       std::max(1, COLS - 2), 1, 1);
  input_inner_ =
      derwin(input_outer_, input_rows, std::max(1, COLS - 2), 1, 1);

  keypad(input_inner_, TRUE);
}

void App::render_frame() {
  // Status header.
  werase(header_win_);
  wbkgd(header_win_, theme::status_attr());
  std::string status = status_line(agent_.config(), busy_,
                                   agent_.last_total_tokens(),
                                   agent_.context_size(),
                                   agent_.last_tokens_per_second());
  mvwaddnstr(header_win_, 0, 0, status.c_str(), COLS);
  wnoutrefresh(header_win_);

  // Chat box frame, then its content.
  werase(chat_outer_);
  wattron(chat_outer_, theme::border_attr());
  box(chat_outer_, 0, 0);
  wattroff(chat_outer_, theme::border_attr());
  wnoutrefresh(chat_outer_);
  chat_.render(chat_inner_);

  // Input box frame, then its content.
  werase(input_outer_);
  wattron(input_outer_, theme::border_attr());
  box(input_outer_, 0, 0);
  wattroff(input_outer_, theme::border_attr());
  wnoutrefresh(input_outer_);
  input_.render(input_inner_, busy_ ? "… " : "> ");
}

void App::show_help() {
  const std::vector<std::string> lines = help_lines();
  int longest = 0;
  for (const auto& l : lines) longest = std::max(longest, (int)l.size());

  const int h = std::min(LINES, (int)lines.size() + 2);
  const int w = std::min(COLS, longest + 4);
  WINDOW* win = newwin(h, w, (LINES - h) / 2, (COLS - w) / 2);
  wattron(win, theme::border_attr());
  box(win, 0, 0);
  wattroff(win, theme::border_attr());
  for (int i = 0; i < (int)lines.size() && i < h - 2; ++i)
    mvwaddnstr(win, i + 1, 2, lines[i].c_str(), w - 3);
  wrefresh(win);

  // Modal: block until any key, then tear down and let the loop repaint.
  keypad(win, TRUE);
  wtimeout(win, -1);
  wgetch(win);
  delwin(win);
  touchwin(stdscr);
  redrawwin(chat_outer_);
  redrawwin(input_outer_);
}

void App::handle_user_input(const std::string& line) {
  ExpandResult ex = expand_attachments(
      line, [](const std::string& path) { return read_file_opt(path); },
      agent_.config().max_image_bytes);
  if (!ex.errors.empty()) {
    for (const auto& e : ex.errors)
      chat_.add("", e, ChatView::Style::Error);
    return;  // a referenced file was unreadable: surface it, don't send
  }
  last_user_message_ = line;  // retry re-expands from the raw line
  submit(line, ex.text, std::move(ex.images));
  if (!ex.attached.empty()) {
    std::string note = "attached:";
    for (const auto& p : ex.attached) note += " " + p;
    chat_.add("", note, ChatView::Style::Dim);
  }
}

namespace {
// "12 KB" / "1.2 MB"-style size for the image placeholder line.
std::string human_size(std::size_t bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";
  double kb = static_cast<double>(bytes) / 1024.0;
  if (kb < 1024.0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f KB", kb);
    return buf;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f MB", kb / 1024.0);
  return buf;
}
}  // namespace

void App::submit(const std::string& display, const std::string& payload,
                 std::vector<ImagePart> images) {
  chat_.add("> ", display);
  // The TUI can't render pixels — show a placeholder line per attached image.
  for (const ImagePart& img : images)
    chat_.add("", "🖼 " + img.name + " (" + human_size(img.bytes) + ")",
              ChatView::Style::Dim);
  thinking_idx_.reset();
  content_idx_.reset();
  busy_ = true;
  cancel_requested_.store(false);  // fresh turn: clear any prior cancel

  AgentCallbacks cb;
  cb.on_content = [this](std::string_view d) {
    events_.push({Event::Type::Delta, std::string(d), "", nullptr});
  };
  cb.on_reasoning = [this](std::string_view d) {
    events_.push({Event::Type::Reasoning, std::string(d), "", nullptr});
  };
  cb.on_tool_call = [this](const ToolCall& call) {
    events_.push(
        {Event::Type::ToolInfo, "↳ " + call.name + " " + call.arguments, "",
         nullptr});
  };
  cb.on_tool_result = [this](const ToolCall& call, const ToolResult& tr) {
    // Show the full output in a collapsible block (header carries the status).
    // Cap the *displayed* body so a huge result can't bloat the view; the model
    // still received the untruncated output via history.
    constexpr std::size_t kMaxDisplay = 4000;
    std::string out = tr.output;
    if (out.size() > kMaxDisplay)
      out = out.substr(0, kMaxDisplay) + "\n… [truncated]";
    const std::string header = call.name + (tr.ok ? " ✓" : " ✗");
    events_.push({Event::Type::ToolResult, std::move(out), header, nullptr});
  };

  worker_ = std::thread([this, payload, images = std::move(images), cb]() mutable {
    ApiResult res =
        agent_.send(payload, std::move(images), cb, &cancel_requested_);
    if (res.ok) {
      events_.push({Event::Type::Done, "", "", nullptr});
    } else if (res.canceled) {
      events_.push({Event::Type::Canceled, "", "", nullptr});
    } else {
      events_.push({Event::Type::Error, res.error, "", nullptr});
    }
  });
}

void App::run_init() {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists("AGENTS.md", ec)) {
    ConfirmDialog dialog;  // we're on the UI thread here
    if (dialog.ask("overwrite AGENTS.md",
                   "AGENTS.md already exists — overwrite it with a generated "
                   "one?") == ConfirmChoice::No) {
      chat_.add("", "/init cancelled.", ChatView::Style::Dim);
      return;
    }
  }
  chat_.add("", "Scanning the project and generating AGENTS.md…",
            ChatView::Style::Dim);
  init_pending_ = true;
  submit("/init", initPrompt(project_tree(".")));
}

void App::finish_init() {
  std::string content;
  const std::vector<Message>& h = agent_.history();
  for (auto it = h.rbegin(); it != h.rend(); ++it) {
    if (it->role == Role::Assistant && !it->content.empty()) {
      content = stripCodeFence(it->content);
      break;
    }
  }
  if (content.empty()) {
    chat_.add("", "/init produced no content.", ChatView::Style::Error);
    return;
  }
  std::ofstream f("AGENTS.md");
  if (!f) {
    chat_.add("", "Could not write AGENTS.md.", ChatView::Style::Error);
    return;
  }
  f << content;
  if (content.back() != '\n') f << '\n';

  // Apply it live as the system prompt for the next conversation.
  agent_.set_system_prompt(content);
  chat_.add("", "Wrote AGENTS.md. Use /clear to start a conversation with it as "
                "the system prompt.",
            ChatView::Style::Dim);
}

void App::run_skill(const std::string& arg) {
  // Split "name [extra user text]".
  std::string name = arg;
  std::string extra;
  if (const std::size_t sp = arg.find_first_of(" \t");
      sp != std::string::npos) {
    name = arg.substr(0, sp);
    if (const std::size_t b = arg.find_first_not_of(" \t", sp);
        b != std::string::npos) {
      extra = arg.substr(b);
    }
  }

  if (name.empty()) {  // /skill with no argument: list what's available
    if (skills_.empty()) {
      chat_.add("", "No skills found. Add *.md files under ./skills or "
                    "~/.config/SimpleCoder/skills.",
                ChatView::Style::Dim);
      return;
    }
    chat_.add("", "Available skills (run with /<name>):", ChatView::Style::Dim);
    for (const Skill& s : skills_.all()) {
      std::string line = "  /" + s.name;
      if (!s.description.empty()) line += " — " + s.description;
      chat_.add("", line, ChatView::Style::Dim);
    }
    return;
  }

  const Skill* s = skills_.find(name);
  if (!s) {
    chat_.add("", "Unknown skill: " + name + "  (try /skill to list)",
              ChatView::Style::Error);
    return;
  }
  std::string payload = s->body;
  if (!extra.empty()) payload += "\n\n" + extra;
  submit("/" + s->name + (extra.empty() ? "" : " " + extra), payload);
}

void App::run_compact() {
  // Need at least one non-system message to have something to summarize.
  bool has_turns = false;
  for (const Message& m : agent_.history())
    if (m.role != Role::System) { has_turns = true; break; }
  if (!has_turns) {
    chat_.add("", "Nothing to compact yet.", ChatView::Style::Dim);
    return;
  }
  chat_.add("", "Summarizing the conversation to compact the context…",
            ChatView::Style::Dim);
  compact_pending_ = true;
  submit("/compact", compactPrompt());
}

void App::finish_compact() {
  std::string summary;
  const std::vector<Message>& h = agent_.history();
  for (auto it = h.rbegin(); it != h.rend(); ++it) {
    if (it->role == Role::Assistant && !it->content.empty()) {
      summary = it->content;
      break;
    }
  }
  if (summary.empty()) {
    chat_.add("", "/compact produced no summary; context left unchanged.",
              ChatView::Style::Error);
    return;
  }
  agent_.compact_into_summary(summary);
  chat_.add("", "✓ Context compacted — the model now continues from the "
                "summary above.",
            ChatView::Style::Dim);
}

void App::maybe_auto_compact() {
  const Config& c = agent_.config();
  if (!c.auto_compact) return;
  if (!context_is_full(agent_.last_total_tokens(), agent_.context_size(),
                       c.auto_compact_threshold)) {
    return;
  }
  chat_.add("", "Context is nearly full — auto-compacting…",
            ChatView::Style::Dim);
  run_compact();
}

void App::drain_events() {
  Event ev;
  while (events_.try_pop(ev)) {
    switch (ev.type) {
      case Event::Type::Reasoning:
        if (!thinking_idx_) thinking_idx_ = chat_.add_thinking();
        chat_.append_to(*thinking_idx_, ev.text);
        break;
      case Event::Type::Delta:
        if (!content_idx_) {
          // The answer is starting: fold the reasoning out of the way.
          if (thinking_idx_) chat_.set_collapsed(*thinking_idx_, true);
          content_idx_ = chat_.add("* ", "");
        }
        chat_.append_to(*content_idx_, ev.text);
        break;
      case Event::Type::ToolInfo:
        chat_.add("", ev.text, ChatView::Style::Dim);
        content_idx_.reset();  // a later answer starts a fresh entry
        break;
      case Event::Type::ToolResult:
        chat_.add_tool_output(ev.tool, ev.text);  // collapsed by default
        content_idx_.reset();
        break;
      case Event::Type::ConfirmRequest: {
        ConfirmDialog dialog;
        ev.reply->set_value(dialog.ask(ev.tool, ev.text));
        break;
      }
      case Event::Type::Done: {
        if (thinking_idx_) chat_.set_collapsed(*thinking_idx_, true);
        join_worker();
        busy_ = false;
        const bool was_init = init_pending_;
        const bool was_compact = compact_pending_;
        if (init_pending_) {
          finish_init();
          init_pending_ = false;
        }
        if (compact_pending_) {
          finish_compact();
          compact_pending_ = false;
        }
        // Only after an ordinary turn — never recurse off /init or /compact.
        if (!was_init && !was_compact) maybe_auto_compact();
        break;
      }
      case Event::Type::Canceled:
        if (thinking_idx_) chat_.set_collapsed(*thinking_idx_, true);
        join_worker();
        busy_ = false;
        init_pending_ = false;     // abandon any pending post-turn action
        compact_pending_ = false;
        chat_.add("", "⏹ Generation cancelled.", ChatView::Style::Dim);
        break;
      case Event::Type::Error:
        join_worker();
        busy_ = false;
        init_pending_ = false;     // abandon any pending post-turn action
        compact_pending_ = false;
        chat_.add("error", ev.text, ChatView::Style::Error);
        log().error("request failed: " + ev.text);
        break;
    }
  }
}

int App::run() {
  Tui tui;
  rebuild_windows();

  const Config& cfg = agent_.config();
  std::string banner;
  for (const auto& line : banner_lines(cfg)) {
    if (!banner.empty()) banner += '\n';
    banner += line;
  }
  chat_.add_preformatted(banner, ChatView::Style::Accent);

  bool running = true;
  while (running) {
    // Resize the input box if the number of visual rows changed (newlines or
    // soft wraps). The inner width and 2-column "> " prompt match render().
    const int want =
        std::clamp(input_.visual_line_count(std::max(1, COLS - 2), 2), 1, 6);
    if (want != input_lines_shown_) rebuild_windows();

    render_frame();
    doupdate();

    wtimeout(input_inner_, 50);
    wint_t wch = 0;
    const int rc = wget_wch(input_inner_, &wch);

    if (rc == ERR) {
      drain_events();
      continue;
    }
    // A printable (possibly multibyte) character: insert it into the prompt.
    if (rc == OK && wch >= 32 && wch != 127) {
      if (!busy_) input_.insert_text(utf8::encode(static_cast<char32_t>(wch)));
      continue;
    }
    // Otherwise it's a control character or a special key code.
    const int ch = static_cast<int>(wch);
    if (ch == KEY_RESIZE) {
      rebuild_windows();
      continue;
    }
    if (ch == KEY_F(1)) {
      show_help();
      continue;
    }
    // Esc aborts an in-flight generation. (Only meaningful while busy; when
    // idle, Esc is the Alt-Enter prefix handled below.)
    if (busy_ && ch == 27) {
      if (!cancel_requested_.exchange(true))
        chat_.add("", "⏹ Cancelling…", ChatView::Style::Dim);
      continue;
    }
    // Page/wheel scrolling and the reasoning toggle always drive the chat,
    // even mid-stream.
    if (ch == KEY_PPAGE || ch == KEY_NPAGE) {
      int rows = 0, cols = 0;
      getmaxyx(chat_inner_, rows, cols);
      (void)cols;
      if (ch == KEY_PPAGE)
        chat_.scroll_page_up(rows);
      else
        chat_.scroll_page_down(rows);
      continue;
    }
    if (ch == '\t') {
      chat_.toggle_last_collapsible();
      continue;
    }
    if (ch == KEY_MOUSE) {
      MEVENT ev;
      if (getmouse(&ev) == OK) {
        if (ev.bstate & BUTTON4_PRESSED)
          chat_.scroll_up(3);  // wheel up
        else if (ev.bstate & BUTTON5_PRESSED)
          chat_.scroll_down(3);  // wheel down
      }
      continue;
    }

    // Arrows / Home / End move the input cursor while typing, and browse the
    // chat history when the prompt is empty (so scrollback still works while a
    // reply streams, since the buffer is empty then).
    const bool editing = !input_.buffer().empty();
    if (ch == KEY_UP) {
      if (editing) input_.handle_key(ch);
      else chat_.scroll_up(1);
      continue;
    }
    if (ch == KEY_DOWN) {
      if (editing) input_.handle_key(ch);
      else chat_.scroll_down(1);
      continue;
    }
    if (ch == KEY_LEFT || ch == KEY_RIGHT) {
      if (editing) input_.handle_key(ch);
      continue;
    }
    if (ch == KEY_HOME) {
      if (editing) input_.handle_key(ch);
      else chat_.scroll_to_top();
      continue;
    }
    if (ch == KEY_END) {
      if (editing) input_.handle_key(ch);
      else chat_.scroll_to_bottom();
      continue;
    }

    if (busy_) {
      drain_events();  // ignore typing while a reply is in flight
      continue;
    }

    if (ch == 27) {  // ESC — check for Alt-Enter (newline) vs a stray escape
      wtimeout(input_inner_, 0);  // non-blocking peek; reset at loop top
      wint_t next = 0;
      const int nrc = wget_wch(input_inner_, &next);
      if (next == '\n' || next == '\r' || next == KEY_ENTER)
        input_.insert_newline();
      else if (nrc == OK && next >= 32 && next != 127)
        input_.insert_text(utf8::encode(static_cast<char32_t>(next)));
      else if (nrc != ERR)
        input_.handle_key(static_cast<int>(next));
      continue;
    }

    if (auto submitted = input_.handle_key(ch)) {
      const Command cmd = parse_command(*submitted);
      switch (cmd.kind) {
        case CommandKind::None:
          if (!submitted->empty()) handle_user_input(*submitted);
          break;
        case CommandKind::Quit:
          running = false;
          break;
        case CommandKind::Help:
          show_help();
          break;
        case CommandKind::Clear:
          chat_.clear();
          agent_.reset();
          break;
        case CommandKind::Retry:
          if (last_user_message_.empty())
            chat_.add("", "Nothing to retry yet.", ChatView::Style::Dim);
          else
            handle_user_input(last_user_message_);
          break;
        case CommandKind::Model:
          if (cmd.arg.empty()) {
            const std::string cur = agent_.config().model;
            const std::vector<std::string> models = agent_.list_models();
            if (models.empty()) {
              chat_.add("",
                        "Current model: " + (cur.empty() ? "(server default)" : cur),
                        ChatView::Style::Dim);
              chat_.add("", "(could not fetch the model list from the server)",
                        ChatView::Style::Dim);
            } else {
              chat_.add("",
                        "Available models (• = current) — /model <name> to switch:",
                        ChatView::Style::Dim);
              for (const auto& m : models)
                chat_.add("", (m == cur ? "  • " : "    ") + m,
                          ChatView::Style::Dim);
            }
          } else {
            agent_.set_model(cmd.arg);
            chat_.add("", "Model set to " + cmd.arg + ".", ChatView::Style::Dim);
          }
          break;
        case CommandKind::Init:
          run_init();
          break;
        case CommandKind::Compact:
          run_compact();
          break;
        case CommandKind::Skill:
          run_skill(cmd.arg);
          break;
        case CommandKind::Unknown:
          // A bare /<word> that names a skill runs it; otherwise it's unknown.
          if (skills_.find(cmd.arg))
            run_skill(cmd.arg);
          else
            chat_.add("", "Unknown command: /" + cmd.arg + "  (try /help)",
                      ChatView::Style::Error);
          break;
      }
    }
  }

  join_worker();
  return 0;
}

}  // namespace llmcli
