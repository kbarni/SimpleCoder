#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "agent/Tool.hpp"

namespace llmcli {

// The user's answer to a confirmation prompt.
enum class ConfirmChoice { Yes, No, Always };

// Asks the user to approve running `tool_name`, showing `details` (e.g. the
// command or target path). Implemented by the UI (ConfirmDialog) in the app and
// stubbed in tests.
using Confirmer =
    std::function<ConfirmChoice(const std::string& tool_name,
                                const std::string& details)>;

// The verdict of the config-driven policy for one tool call, consulted before
// the session "always allow" set and before prompting.
enum class PolicyDecision { Prompt, Allow, Deny };

// A pure, config-driven set of allow/deny rules that pre-decide gated tool calls
// (T30), cutting confirmation fatigue. A rule is either a bare tool name (the
// whole tool) or `<tool>:<prefix>` (matches when `details` starts with
// `<prefix>` — for `run_bash`, `details` is the command). Deny always wins, so a
// deny rule blocks even an otherwise-allowed match. Kept separate from
// ConfirmGate so the matcher is unit-testable without a Confirmer.
class ConfirmPolicy {
 public:
  // Add a rule from config. Empty rules are ignored. A `<tool>:<prefix>` form
  // splits on the first ':'; everything after it (verbatim) is the prefix.
  void add_allow(const std::string& rule);
  void add_deny(const std::string& rule);

  // Allow / Deny / Prompt for a gated tool call. Deny wins over Allow.
  PolicyDecision decide(const std::string& tool,
                        const std::string& details) const;

 private:
  std::set<std::string> allow_tools_;
  std::set<std::string> deny_tools_;
  std::map<std::string, std::vector<std::string>> allow_prefixes_;
  std::map<std::string, std::vector<std::string>> deny_prefixes_;
};

// Build a policy from the accumulated config `allow`/`deny` rule lists.
ConfirmPolicy makeConfirmPolicy(const std::vector<std::string>& allow_rules,
                                const std::vector<std::string>& deny_rules);

// Decides whether a tool call may proceed, combining each tool's safety policy,
// the config-driven allow/deny policy, and the user's prior "always allow"
// decisions for this session.
//
//   - Tools that don't require confirmation always pass without prompting.
//   - The config policy is consulted first: a deny rule blocks outright, an
//     allow rule runs without prompting.
//   - Otherwise a gated tool prompts via the Confirmer; "Always" remembers the
//     tool so it won't prompt again this session.
class ConfirmGate {
 public:
  explicit ConfirmGate(Confirmer confirmer, ConfirmPolicy policy = {});

  // Returns true if `tool` may run now (possibly after prompting).
  bool allow(const Tool& tool, const std::string& details);

  // Test/inspection helpers.
  bool always_allowed(const std::string& tool_name) const;

 private:
  Confirmer confirmer_;
  ConfirmPolicy policy_;
  std::set<std::string> always_allowed_;
};

}  // namespace llmcli
