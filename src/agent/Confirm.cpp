#include "agent/Confirm.hpp"

namespace llmcli {

namespace {

// Split a rule into (tool, prefix). With no ':' the whole string is the tool and
// `has_prefix` is false; otherwise prefix is everything after the first ':'
// (taken verbatim, so a command like "git log --oneline" survives intact).
struct Rule {
  std::string tool;
  std::string prefix;
  bool has_prefix = false;
};

Rule split_rule(const std::string& rule) {
  const std::size_t colon = rule.find(':');
  if (colon == std::string::npos) return {rule, "", false};
  return {rule.substr(0, colon), rule.substr(colon + 1), true};
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

}  // namespace

void ConfirmPolicy::add_allow(const std::string& rule) {
  if (rule.empty()) return;
  const Rule r = split_rule(rule);
  if (r.has_prefix)
    allow_prefixes_[r.tool].push_back(r.prefix);
  else
    allow_tools_.insert(r.tool);
}

void ConfirmPolicy::add_deny(const std::string& rule) {
  if (rule.empty()) return;
  const Rule r = split_rule(rule);
  if (r.has_prefix)
    deny_prefixes_[r.tool].push_back(r.prefix);
  else
    deny_tools_.insert(r.tool);
}

PolicyDecision ConfirmPolicy::decide(const std::string& tool,
                                     const std::string& details) const {
  // Deny wins over everything.
  if (deny_tools_.count(tool)) return PolicyDecision::Deny;
  if (auto it = deny_prefixes_.find(tool); it != deny_prefixes_.end()) {
    for (const std::string& p : it->second)
      if (starts_with(details, p)) return PolicyDecision::Deny;
  }

  if (allow_tools_.count(tool)) return PolicyDecision::Allow;
  if (auto it = allow_prefixes_.find(tool); it != allow_prefixes_.end()) {
    for (const std::string& p : it->second)
      if (starts_with(details, p)) return PolicyDecision::Allow;
  }

  return PolicyDecision::Prompt;
}

ConfirmPolicy makeConfirmPolicy(const std::vector<std::string>& allow_rules,
                                const std::vector<std::string>& deny_rules) {
  ConfirmPolicy policy;
  for (const std::string& r : allow_rules) policy.add_allow(r);
  for (const std::string& r : deny_rules) policy.add_deny(r);
  return policy;
}

ConfirmGate::ConfirmGate(Confirmer confirmer, ConfirmPolicy policy)
    : confirmer_(std::move(confirmer)), policy_(std::move(policy)) {}

bool ConfirmGate::allow(const Tool& tool, const std::string& details) {
  if (!tool.requires_confirmation()) return true;

  const std::string name = tool.name();

  // Config-driven policy is consulted before the session set and before
  // prompting; a deny rule blocks even a previously "always allowed" tool.
  switch (policy_.decide(name, details)) {
    case PolicyDecision::Deny:
      return false;
    case PolicyDecision::Allow:
      return true;
    case PolicyDecision::Prompt:
      break;
  }

  if (always_allowed_.count(name)) return true;

  // No confirmer wired up: fail closed on a gated tool.
  if (!confirmer_) return false;

  switch (confirmer_(name, details)) {
    case ConfirmChoice::Yes:
      return true;
    case ConfirmChoice::Always:
      always_allowed_.insert(name);
      return true;
    case ConfirmChoice::No:
      return false;
  }
  return false;
}

bool ConfirmGate::always_allowed(const std::string& tool_name) const {
  return always_allowed_.count(tool_name) > 0;
}

}  // namespace llmcli
