#include "agent/Confirm.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "agent/Tool.hpp"

using llmcli::ConfirmChoice;
using llmcli::ConfirmGate;
using llmcli::ConfirmPolicy;
using llmcli::makeConfirmPolicy;
using llmcli::PolicyDecision;
using llmcli::ReadFileTool;
using llmcli::RunBashTool;
using llmcli::WriteFileTool;

namespace {

// A confirmer that returns a fixed choice and counts how often it is asked.
struct StubConfirmer {
  ConfirmChoice answer = ConfirmChoice::Yes;
  int calls = 0;
  std::vector<std::string> seen_tools;

  ConfirmChoice operator()(const std::string& tool, const std::string&) {
    ++calls;
    seen_tools.push_back(tool);
    return answer;
  }
};

}  // namespace

TEST_CASE("non-gated tools run without prompting", "[confirm]") {
  StubConfirmer stub;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); });

  ReadFileTool read;
  CHECK(gate.allow(read, "anything"));
  CHECK(stub.calls == 0);
}

TEST_CASE("gated tool: Yes allows once, prompts again next time", "[confirm]") {
  StubConfirmer stub;
  stub.answer = ConfirmChoice::Yes;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); });

  RunBashTool bash;
  CHECK(gate.allow(bash, "rm -rf /tmp/x"));
  CHECK(gate.allow(bash, "ls"));
  CHECK(stub.calls == 2);  // Yes is per-call, not remembered
  CHECK_FALSE(gate.always_allowed("run_bash"));
}

TEST_CASE("gated tool: No denies", "[confirm]") {
  StubConfirmer stub;
  stub.answer = ConfirmChoice::No;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); });

  WriteFileTool write;
  CHECK_FALSE(gate.allow(write, "/etc/passwd"));
  CHECK(stub.calls == 1);
}

TEST_CASE("gated tool: Always suppresses later prompts for that tool",
          "[confirm]") {
  StubConfirmer stub;
  stub.answer = ConfirmChoice::Always;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); });

  RunBashTool bash;
  CHECK(gate.allow(bash, "echo 1"));
  CHECK(gate.always_allowed("run_bash"));
  CHECK(gate.allow(bash, "echo 2"));
  CHECK(gate.allow(bash, "echo 3"));
  CHECK(stub.calls == 1);  // only the first prompted
}

TEST_CASE("always-allow is per tool, not global", "[confirm]") {
  StubConfirmer stub;
  stub.answer = ConfirmChoice::Always;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); });

  RunBashTool bash;
  WriteFileTool write;
  gate.allow(bash, "x");          // remembers run_bash
  CHECK(gate.allow(write, "y"));  // write still prompts
  CHECK(stub.calls == 2);
  CHECK(gate.always_allowed("run_bash"));
  CHECK(gate.always_allowed("write_file"));  // because stub also said Always
}

TEST_CASE("a gate with no confirmer fails closed on gated tools",
          "[confirm]") {
  ConfirmGate gate(nullptr);
  RunBashTool bash;
  ReadFileTool read;
  CHECK_FALSE(gate.allow(bash, "x"));  // gated + no confirmer -> denied
  CHECK(gate.allow(read, "x"));        // non-gated still fine
}

// --- T30 allow-list policy ---------------------------------------------------

TEST_CASE("policy: whole-tool allow and command-prefix allow", "[confirm]") {
  ConfirmPolicy p;
  p.add_allow("write_file");          // whole tool
  p.add_allow("run_bash:git status"); // command prefix

  CHECK(p.decide("write_file", "path: anything") == PolicyDecision::Allow);
  CHECK(p.decide("run_bash", "git status -s") == PolicyDecision::Allow);
  CHECK(p.decide("run_bash", "rm -rf /") == PolicyDecision::Prompt);
  CHECK(p.decide("run_bash", "echo git status") == PolicyDecision::Prompt);
}

TEST_CASE("policy: deny wins over allow", "[confirm]") {
  ConfirmPolicy p;
  p.add_allow("run_bash:git");   // would allow all git commands
  p.add_deny("run_bash:git push");

  CHECK(p.decide("run_bash", "git log") == PolicyDecision::Allow);
  CHECK(p.decide("run_bash", "git push origin") == PolicyDecision::Deny);
}

TEST_CASE("policy: empty rules are ignored", "[confirm]") {
  ConfirmPolicy p;
  p.add_allow("");
  p.add_deny("");
  CHECK(p.decide("run_bash", "anything") == PolicyDecision::Prompt);
}

TEST_CASE("gate: allowed pattern runs without prompting", "[confirm]") {
  StubConfirmer stub;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); },
                   makeConfirmPolicy({"run_bash:ls"}, {}));

  RunBashTool bash;
  CHECK(gate.allow(bash, "ls -la"));   // matches prefix -> no prompt
  CHECK(gate.allow(bash, "cat /etc"));  // no match -> prompts (stub says Yes)
  CHECK(stub.calls == 1);
}

TEST_CASE("gate: deny rule blocks even an always-allowed tool", "[confirm]") {
  StubConfirmer stub;
  stub.answer = ConfirmChoice::Always;
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); },
                   makeConfirmPolicy({}, {"run_bash:rm"}));

  RunBashTool bash;
  CHECK(gate.allow(bash, "echo hi"));   // prompts, Always remembers run_bash
  CHECK(gate.always_allowed("run_bash"));
  CHECK_FALSE(gate.allow(bash, "rm -rf x"));  // deny wins over the session set
  CHECK(stub.calls == 1);
}

TEST_CASE("gate: read-only tools are unaffected by policy", "[confirm]") {
  StubConfirmer stub;
  // A deny rule naming a read-only tool must not block it (non-gated always
  // passes without consulting the policy).
  ConfirmGate gate([&](auto& t, auto& d) { return stub(t, d); },
                   makeConfirmPolicy({}, {"read_file"}));

  ReadFileTool read;
  CHECK(gate.allow(read, "secret.txt"));
  CHECK(stub.calls == 0);
}
