#include "app/SimpleApp.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "app/Attachments.hpp"
#include "app/Command.hpp"
#include "app/SystemPrompt.hpp"
#include "ui/Banner.hpp"
#include "util/Log.hpp"

namespace llmcli {

namespace {

std::optional<std::string> read_file_opt(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

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

std::string human_size(std::size_t bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";
  const double kb = static_cast<double>(bytes) / 1024.0;
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

SimpleApp::SimpleApp(Config cfg)
    : agent_(std::move(cfg), [this](const std::string& tool,
                                    const std::string& details) {
        return request_confirmation(tool, details);
      }),
      skills_(discoverSkills()) {}

SimpleApp::~SimpleApp() { join_worker(); }

void SimpleApp::join_worker() {
  if (worker_.joinable()) worker_.join();
}

ConfirmChoice SimpleApp::request_confirmation(const std::string& tool,
                                              const std::string& details) {
  auto reply = std::make_shared<std::promise<ConfirmChoice>>();
  std::future<ConfirmChoice> fut = reply->get_future();
  events_.push({Event::Type::ConfirmRequest, details, tool, reply});
  return fut.get();
}

void SimpleApp::submit(const std::string& payload,
                       std::vector<ImagePart> images) {
  in_thinking_ = false;
  in_content_ = false;
  busy_ = true;
  cancel_requested_.store(false);

  AgentCallbacks cb;
  cb.on_content = [this](std::string_view d) {
    events_.push({Event::Type::Delta, std::string(d), "", nullptr});
  };
  cb.on_reasoning = [this](std::string_view d) {
    events_.push({Event::Type::Reasoning, std::string(d), "", nullptr});
  };
  cb.on_tool_call = [this](const ToolCall& call) {
    events_.push({Event::Type::ToolInfo,
                  "↳ " + call.name + " " + call.arguments, "", nullptr});
  };
  cb.on_tool_result = [this](const ToolCall& call, const ToolResult& tr) {
    constexpr std::size_t kMaxDisplay = 4000;
    std::string out = tr.output;
    if (out.size() > kMaxDisplay)
      out = out.substr(0, kMaxDisplay) + "\n… [truncated]";
    events_.push({Event::Type::ToolResult, std::move(out),
                  call.name + (tr.ok ? " ✓" : " ✗"), nullptr});
  };

  worker_ = std::thread([this, payload, images = std::move(images),
                         cb]() mutable {
    ApiResult res =
        agent_.send(payload, std::move(images), cb, &cancel_requested_);
    if (res.ok)
      events_.push({Event::Type::Done, "", "", nullptr});
    else if (res.canceled)
      events_.push({Event::Type::Canceled, "", "", nullptr});
    else
      events_.push({Event::Type::Error, res.error, "", nullptr});
  });
}

void SimpleApp::handle_user_input(const std::string& line) {
  ExpandResult ex = expand_attachments(
      line, [](const std::string& path) { return read_file_opt(path); },
      agent_.config().max_image_bytes);
  if (!ex.errors.empty()) {
    for (const auto& e : ex.errors) std::cerr << e << '\n';
    return;
  }
  last_user_message_ = line;
  if (!ex.attached.empty()) {
    std::string note = "attached:";
    for (const auto& p : ex.attached) note += " " + p;
    std::cout << note << '\n';
  }
  for (const ImagePart& img : ex.images)
    std::cout << "\U0001f5bc " << img.name << " (" << human_size(img.bytes)
              << ")\n";
  submit(ex.text, std::move(ex.images));
}

void SimpleApp::run_init() {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists("AGENTS.md", ec)) {
    std::cout << "AGENTS.md already exists — overwrite? [y/N]: "
              << std::flush;
    std::string ans;
    if (!std::getline(std::cin, ans) ||
        ans.empty() ||
        std::tolower(static_cast<unsigned char>(ans[0])) != 'y') {
      std::cout << "/init cancelled.\n";
      return;
    }
  }
  std::cout << "Scanning the project and generating AGENTS.md…\n";
  init_pending_ = true;
  submit(initPrompt(project_tree(".")));
}

void SimpleApp::finish_init() {
  std::string content;
  const std::vector<Message>& h = agent_.history();
  for (auto it = h.rbegin(); it != h.rend(); ++it) {
    if (it->role == Role::Assistant && !it->content.empty()) {
      content = stripCodeFence(it->content);
      break;
    }
  }
  if (content.empty()) {
    std::cerr << "/init produced no content.\n";
    return;
  }
  std::ofstream f("AGENTS.md");
  if (!f) {
    std::cerr << "Could not write AGENTS.md.\n";
    return;
  }
  f << content;
  if (content.back() != '\n') f << '\n';
  agent_.set_system_prompt(content);
  std::cout << "Wrote AGENTS.md. Use /clear to start a conversation with it "
               "as the system prompt.\n";
}

void SimpleApp::run_skill(const std::string& arg) {
  std::string name = arg;
  std::string extra;
  if (const std::size_t sp = arg.find_first_of(" \t");
      sp != std::string::npos) {
    name = arg.substr(0, sp);
    if (const std::size_t b = arg.find_first_not_of(" \t", sp);
        b != std::string::npos)
      extra = arg.substr(b);
  }

  if (name.empty()) {
    if (skills_.empty()) {
      std::cout << "No skills found. Add *.md files under ./skills or "
                   "~/.config/SimpleCoder/skills.\n";
      return;
    }
    std::cout << "Available skills (run with /<name>):\n";
    for (const Skill& s : skills_.all()) {
      std::cout << "  /" << s.name;
      if (!s.description.empty()) std::cout << " — " << s.description;
      std::cout << '\n';
    }
    return;
  }

  const Skill* s = skills_.find(name);
  if (!s) {
    std::cerr << "Unknown skill: " << name << "  (try /skill to list)\n";
    return;
  }
  std::string payload = s->body;
  if (!extra.empty()) payload += "\n\n" + extra;
  submit(payload);
}

void SimpleApp::run_compact() {
  bool has_turns = false;
  for (const Message& m : agent_.history())
    if (m.role != Role::System) {
      has_turns = true;
      break;
    }
  if (!has_turns) {
    std::cout << "Nothing to compact yet.\n";
    return;
  }
  std::cout << "Summarizing the conversation to compact the context…\n";
  compact_pending_ = true;
  submit(compactPrompt());
}

void SimpleApp::finish_compact() {
  std::string summary;
  const std::vector<Message>& h = agent_.history();
  for (auto it = h.rbegin(); it != h.rend(); ++it) {
    if (it->role == Role::Assistant && !it->content.empty()) {
      summary = it->content;
      break;
    }
  }
  if (summary.empty()) {
    std::cerr << "/compact produced no summary; context left unchanged.\n";
    return;
  }
  agent_.compact_into_summary(summary);
  std::cout << "✓ Context compacted.\n";
}

void SimpleApp::maybe_auto_compact() {
  const Config& c = agent_.config();
  if (!c.auto_compact) return;
  if (!context_is_full(agent_.last_total_tokens(), agent_.context_size(),
                       c.auto_compact_threshold))
    return;
  std::cout << "Context is nearly full — auto-compacting…\n";
  run_compact();
}

void SimpleApp::drain_events() {
  // Loops until busy_ becomes false (set in Done/Error/Canceled handlers).
  // maybe_auto_compact() inside the Done handler may call submit() and set
  // busy_ = true again, which is why we re-check the condition rather than
  // exiting immediately after Done.
  while (busy_) {
    auto opt = events_.pop();
    if (!opt) break;
    Event& ev = *opt;

    switch (ev.type) {
      case Event::Type::Reasoning:
        if (!in_thinking_) {
          std::cout << "[thinking] " << std::flush;
          in_thinking_ = true;
        }
        std::cout << ev.text << std::flush;
        break;

      case Event::Type::Delta:
        if (in_thinking_) {
          std::cout << "\n\n" << std::flush;
          in_thinking_ = false;
        }
        std::cout << ev.text << std::flush;
        in_content_ = true;
        break;

      case Event::Type::ToolInfo:
        if (in_content_) { std::cout << '\n'; in_content_ = false; }
        if (in_thinking_) { std::cout << '\n'; in_thinking_ = false; }
        std::cout << ev.text << '\n';
        break;

      case Event::Type::ToolResult:
        // Header line (e.g. "read_file ✓"), then the full output.
        std::cout << ev.tool << '\n' << ev.text << '\n';
        in_content_ = false;
        break;

      case Event::Type::ConfirmRequest: {
        if (in_content_) { std::cout << '\n'; in_content_ = false; }
        if (in_thinking_) { std::cout << '\n'; in_thinking_ = false; }
        std::cout << "\n--- confirm: " << ev.tool << " ---\n"
                  << ev.text << "\n---\n"
                  << "Allow? [y]es / [n]o / [a]lways: " << std::flush;
        std::string ans;
        ConfirmChoice choice = ConfirmChoice::No;
        if (std::getline(std::cin, ans) && !ans.empty()) {
          const char c = static_cast<char>(
              std::tolower(static_cast<unsigned char>(ans[0])));
          if (c == 'y') choice = ConfirmChoice::Yes;
          else if (c == 'a') choice = ConfirmChoice::Always;
        }
        ev.reply->set_value(choice);
        break;
      }

      case Event::Type::Done: {
        if (in_thinking_) { std::cout << '\n'; in_thinking_ = false; }
        if (in_content_) { std::cout << "\n\n"; in_content_ = false; }
        join_worker();
        busy_ = false;
        const bool was_init = init_pending_;
        const bool was_compact = compact_pending_;
        if (init_pending_) { finish_init(); init_pending_ = false; }
        if (compact_pending_) { finish_compact(); compact_pending_ = false; }
        // maybe_auto_compact may call submit() and set busy_ = true; the
        // while loop above will then continue draining the new turn.
        if (!was_init && !was_compact) maybe_auto_compact();
        break;
      }

      case Event::Type::Canceled:
        if (in_thinking_) { std::cout << '\n'; in_thinking_ = false; }
        if (in_content_) { std::cout << '\n'; in_content_ = false; }
        join_worker();
        busy_ = false;
        init_pending_ = false;
        compact_pending_ = false;
        std::cout << "⏹ Generation cancelled.\n";
        break;

      case Event::Type::Error:
        if (in_thinking_) { std::cout << '\n'; in_thinking_ = false; }
        if (in_content_) { std::cout << '\n'; in_content_ = false; }
        join_worker();
        busy_ = false;
        init_pending_ = false;
        compact_pending_ = false;
        std::cerr << "error: " << ev.text << '\n';
        log().error("request failed: " + ev.text);
        break;
    }
  }
}

int SimpleApp::run() {
  const Config& cfg = agent_.config();
  for (const auto& line : banner_lines(cfg)) std::cout << line << '\n';
  std::cout << "  Type /help for commands, /quit to exit.\n\n";

  std::string line;
  while (true) {
    std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    if (line.empty()) continue;

    const Command cmd = parse_command(line);
    switch (cmd.kind) {
      case CommandKind::None:
        handle_user_input(line);
        break;
      case CommandKind::Quit:
        return 0;
      case CommandKind::Help:
        for (const auto& h : help_lines()) std::cout << h << '\n';
        break;
      case CommandKind::Clear:
        agent_.reset();
        std::cout << "Conversation cleared.\n";
        break;
      case CommandKind::Retry:
        if (last_user_message_.empty())
          std::cout << "Nothing to retry yet.\n";
        else
          handle_user_input(last_user_message_);
        break;
      case CommandKind::Model:
        if (cmd.arg.empty()) {
          const std::string cur = agent_.config().model;
          const std::vector<std::string> models = agent_.list_models();
          if (models.empty()) {
            std::cout << "Current model: "
                      << (cur.empty() ? "(server default)" : cur) << '\n'
                      << "(could not fetch the model list from the server)\n";
          } else {
            std::cout << "Available models (• = current) — "
                         "/model <name> to switch:\n";
            for (const auto& m : models)
              std::cout << (m == cur ? "  • " : "    ") << m << '\n';
          }
        } else {
          agent_.set_model(cmd.arg);
          std::cout << "Model set to " << cmd.arg << ".\n";
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
        if (skills_.find(cmd.arg))
          run_skill(cmd.arg);
        else
          std::cerr << "Unknown command: /" << cmd.arg << "  (try /help)\n";
        break;
    }

    // If a command kicked off a worker turn, drain events before the next prompt.
    if (busy_) drain_events();
  }
  return 0;
}

}  // namespace llmcli
