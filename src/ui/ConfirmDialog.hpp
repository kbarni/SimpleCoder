#pragma once

#include <string>

#include "agent/Confirm.hpp"

namespace llmcli {

// A modal y/n/always prompt drawn centered on the screen. Must be called on the
// UI thread (it draws with ncurses and reads a key synchronously).
class ConfirmDialog {
 public:
  // Show the prompt for `tool_name` with `details`, blocking until the user
  // presses y (Yes), n / Esc (No), or a (Always).
  ConfirmChoice ask(const std::string& tool_name,
                    const std::string& details) const;
};

}  // namespace llmcli
