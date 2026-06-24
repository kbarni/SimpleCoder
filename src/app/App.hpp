#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <ncursesw/ncurses.h>

#include "agent/Agent.hpp"
#include "agent/Confirm.hpp"
#include "app/Config.hpp"
#include "app/Skill.hpp"
#include "ui/ChatView.hpp"
#include "ui/InputBar.hpp"
#include "util/ThreadQueue.hpp"

namespace llmcli {

// Top-level controller: owns the agent and the TUI widgets, and runs the event
// loop. A submitted turn runs on a worker thread; streamed deltas, tool
// activity, and confirmation requests flow back through a ThreadQueue and are
// handled on the main (UI) thread, so ncurses is only ever touched there.
class App {
 public:
  explicit App(Config cfg);
  ~App();

  int run();

 private:
  struct Event {
    enum class Type {
      Delta,
      Reasoning,
      ToolInfo,
      ToolResult,
      ConfirmRequest,
      Done,
      Canceled,
      Error
    } type;
    std::string text;  // delta / info / error / confirm details
    std::string tool;  // tool name for ConfirmRequest
    std::shared_ptr<std::promise<ConfirmChoice>> reply;  // ConfirmRequest only
  };

  // Called on the worker thread: posts a confirmation request to the UI and
  // blocks until the UI thread answers.
  ConfirmChoice request_confirmation(const std::string& tool,
                                     const std::string& details);

  void rebuild_windows();
  void render_frame();
  void show_help();  // modal command/key overlay (any key dismisses)
  // Expand `@file` attachments in a typed line and send it, or surface read
  // errors and decline to send.
  void handle_user_input(const std::string& line);
  // Show `display` as the user's turn and send `payload` (plus any image
  // attachments) to the model.
  void submit(const std::string& display, const std::string& payload,
              std::vector<ImagePart> images = {});
  // /init: gather project context and ask the model to generate an AGENTS.md.
  void run_init();
  // Called when an /init turn completes: write the model's reply to AGENTS.md.
  void finish_init();
  // /skill: with an empty arg, list available skills; otherwise run the named
  // skill (arg = "name [extra user text]"), injecting its body into a turn.
  void run_skill(const std::string& arg);
  // /compact: ask the model to summarize the conversation so far.
  void run_compact();
  // Called when a /compact turn completes: replace history with the summary.
  void finish_compact();
  // After a normal turn, auto-run /compact if context usage crossed the
  // configured threshold (no-op when disabled or the window size is unknown).
  void maybe_auto_compact();
  void drain_events();
  void join_worker();

  Agent agent_;
  ChatView chat_;
  InputBar input_;
  ThreadQueue<Event> events_;
  std::thread worker_;
  bool busy_ = false;
  // Set on the UI thread (Esc) to abort the in-flight turn; read by the worker's
  // libcurl progress callback and the tool loop. Reset at the start of a turn.
  std::atomic<bool> cancel_requested_{false};
  std::string last_user_message_;  // for /retry
  int input_lines_shown_ = 1;      // input rows currently laid out
  bool init_pending_ = false;      // an /init turn is in flight
  bool compact_pending_ = false;   // a /compact turn is in flight
  SkillRegistry skills_;           // discovered at startup (./skills + user dir)

  std::optional<std::size_t> thinking_idx_;
  std::optional<std::size_t> content_idx_;

  // Layout: a status header row on top, a bordered chat box, and a bordered
  // input box at the bottom. The *_inner_ windows are derived content areas
  // inset inside the borders, so ChatView/InputBar render at their (0,0).
  WINDOW* header_win_ = nullptr;
  WINDOW* chat_outer_ = nullptr;
  WINDOW* chat_inner_ = nullptr;
  WINDOW* input_outer_ = nullptr;
  WINDOW* input_inner_ = nullptr;
};

}  // namespace llmcli
