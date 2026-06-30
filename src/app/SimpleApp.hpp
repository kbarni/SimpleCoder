#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "agent/Agent.hpp"
#include "agent/Confirm.hpp"
#include "app/Config.hpp"
#include "app/Skill.hpp"
#include "llm/Message.hpp"
#include "util/ThreadQueue.hpp"

namespace llmcli {

// Simplified App that uses plain stdin/stdout instead of ncurses.
// The event/threading model mirrors App: the worker thread pushes Events and
// drain_events() blocks on them with a condition-variable pop.
class SimpleApp {
 public:
  explicit SimpleApp(Config cfg);
  ~SimpleApp();

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
    std::string text;
    std::string tool;
    std::shared_ptr<std::promise<ConfirmChoice>> reply;
  };

  // Posted from the worker thread; blocks it until the main thread answers.
  ConfirmChoice request_confirmation(const std::string& tool,
                                     const std::string& details);

  // Start a worker turn. Sets busy_ = true and returns; the caller must
  // ensure drain_events() is run afterward (either directly or via the
  // outer while-busy loop in an already-running drain_events).
  void submit(const std::string& payload, std::vector<ImagePart> images = {});

  void handle_user_input(const std::string& line);
  void run_init();
  void finish_init();
  void run_skill(const std::string& arg);
  void run_compact();
  void finish_compact();
  void maybe_auto_compact();

  // Blocks (via ThreadQueue::pop) until the current turn — and any
  // auto-compact turn it triggers — finishes.
  void drain_events();

  void join_worker();

  Agent agent_;
  SkillRegistry skills_;
  ThreadQueue<Event> events_;
  std::thread worker_;
  bool busy_ = false;
  std::atomic<bool> cancel_requested_{false};
  std::string last_user_message_;
  bool init_pending_ = false;
  bool compact_pending_ = false;
  // Track whether the last printed character needs a newline before the prompt.
  bool in_thinking_ = false;
  bool in_content_ = false;
};

}  // namespace llmcli
