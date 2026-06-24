#include "agent/Agent.hpp"

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "agent/Confirm.hpp"
#include "agent/Tool.hpp"
#include "llm/Message.hpp"

using llmcli::AgentCallbacks;
using llmcli::ApiResult;
using llmcli::ConfirmChoice;
using llmcli::ConfirmGate;
using llmcli::Message;
using llmcli::Role;
using llmcli::run_tool_loop;
using llmcli::ToolCall;
namespace fs = std::filesystem;

namespace {

// A scripted ChatRound: returns each queued ApiResult in turn, recording the
// history it was called with so the test can assert tool results were fed back.
struct MockRounds {
  std::vector<ApiResult> script;
  std::size_t calls = 0;
  std::vector<std::vector<Message>> seen_histories;

  ApiResult operator()(const std::vector<Message>& history) {
    seen_histories.push_back(history);
    return script.at(calls++);
  }
};

ApiResult toolCallResult(const ToolCall& call) {
  ApiResult r;
  r.ok = true;
  r.finish_reason = "tool_calls";
  r.tool_calls = {call};
  return r;
}

ApiResult finalResult(const std::string& text) {
  ApiResult r;
  r.ok = true;
  r.finish_reason = "stop";
  r.content = text;
  return r;
}

}  // namespace

TEST_CASE("tool loop runs read_file then returns the final answer",
          "[toolloop]") {
  const auto path = fs::temp_directory_path() /
                    ("llm_cli_loop_" + std::to_string(::getpid()) + ".txt");
  { std::ofstream(path) << "the secret is 1234"; }

  MockRounds rounds;
  rounds.script.push_back(toolCallResult(
      {"call_1", "read_file", R"({"path":")" + path.string() + R"("})"}));
  rounds.script.push_back(finalResult("The file says 1234."));

  auto tools = llmcli::default_tools();
  ConfirmGate gate(nullptr);  // read_file is not gated
  AgentCallbacks cb;

  std::vector<Message> history = {Message::user("read the file")};
  ApiResult res =
      run_tool_loop(history, [&](auto& h) { return rounds(h); }, tools, gate, cb);

  REQUIRE(res.ok);
  CHECK(res.content == "The file says 1234.");
  CHECK(rounds.calls == 2);

  // History now: user, assistant(tool_calls), tool result, assistant(final).
  REQUIRE(history.size() == 4);
  CHECK(history[1].role == Role::Assistant);
  CHECK(history[1].tool_calls.size() == 1);
  CHECK(history[2].role == Role::Tool);
  CHECK(history[2].content == "the secret is 1234");
  CHECK(history[3].content == "The file says 1234.");

  // The second round must have seen the tool result in its history.
  REQUIRE(rounds.seen_histories.size() == 2);
  CHECK(rounds.seen_histories[1].back().role == Role::Tool);

  fs::remove(path);
}

TEST_CASE("declined gated tool feeds a refusal back and the loop continues",
          "[toolloop]") {
  MockRounds rounds;
  rounds.script.push_back(toolCallResult(
      {"call_1", "run_bash", R"({"command":"rm -rf /"})"}));
  rounds.script.push_back(finalResult("Okay, I won't run that."));

  auto tools = llmcli::default_tools();
  int prompts = 0;
  ConfirmGate gate([&](auto&, auto&) {
    ++prompts;
    return ConfirmChoice::No;  // user declines
  });

  bool ran_result_cb = false;
  AgentCallbacks cb;
  cb.on_tool_result = [&](const ToolCall&, const llmcli::ToolResult& tr) {
    ran_result_cb = true;
    CHECK_FALSE(tr.ok);
    CHECK(tr.output.find("declined") != std::string::npos);
  };

  std::vector<Message> history = {Message::user("delete everything")};
  ApiResult res =
      run_tool_loop(history, [&](auto& h) { return rounds(h); }, tools, gate, cb);

  REQUIRE(res.ok);
  CHECK(prompts == 1);
  CHECK(ran_result_cb);
  CHECK(res.content == "Okay, I won't run that.");
  // The tool result fed back to the model is the refusal text.
  CHECK(history[2].role == Role::Tool);
  CHECK(history[2].content.find("declined") != std::string::npos);
}

TEST_CASE("an unknown tool name yields an error result, not a crash",
          "[toolloop]") {
  MockRounds rounds;
  rounds.script.push_back(
      toolCallResult({"call_1", "no_such_tool", "{}"}));
  rounds.script.push_back(finalResult("done"));

  auto tools = llmcli::default_tools();
  ConfirmGate gate(nullptr);
  AgentCallbacks cb;

  std::vector<Message> history = {Message::user("hi")};
  ApiResult res =
      run_tool_loop(history, [&](auto& h) { return rounds(h); }, tools, gate, cb);

  REQUIRE(res.ok);
  CHECK(history[2].content.find("unknown tool") != std::string::npos);
}

TEST_CASE("a failed round aborts the loop", "[toolloop]") {
  MockRounds rounds;
  ApiResult fail;
  fail.ok = false;
  fail.error = "boom";
  rounds.script.push_back(fail);

  auto tools = llmcli::default_tools();
  ConfirmGate gate(nullptr);
  AgentCallbacks cb;

  std::vector<Message> history = {Message::user("hi")};
  ApiResult res =
      run_tool_loop(history, [&](auto& h) { return rounds(h); }, tools, gate, cb);

  CHECK_FALSE(res.ok);
  CHECK(rounds.calls == 1);
}

TEST_CASE("a pre-set cancel flag stops the loop before any request",
          "[toolloop][cancel]") {
  MockRounds rounds;
  rounds.script.push_back(finalResult("never requested"));
  auto tools = llmcli::default_tools();
  ConfirmGate gate(nullptr);
  AgentCallbacks cb;
  std::atomic<bool> cancel{true};

  std::vector<Message> history = {Message::user("hi")};
  ApiResult res = run_tool_loop(history, [&](auto& h) { return rounds(h); },
                                tools, gate, cb, /*max_iters=*/8, &cancel);

  CHECK_FALSE(res.ok);
  CHECK(res.canceled);
  CHECK(rounds.calls == 0);  // aborted before issuing the request
}

TEST_CASE("cancelling mid-loop stops before the next request",
          "[toolloop][cancel]") {
  std::atomic<bool> cancel{false};
  MockRounds rounds;
  rounds.script.push_back(
      toolCallResult({"c1", "read_file", R"({"path":"/no/such"})"}));
  rounds.script.push_back(finalResult("should not be reached"));

  auto tools = llmcli::default_tools();
  ConfirmGate gate(nullptr);
  AgentCallbacks cb;

  std::vector<Message> history = {Message::user("go")};
  int round_calls = 0;
  ApiResult res = run_tool_loop(
      history,
      [&](const std::vector<Message>& h) {
        ++round_calls;
        ApiResult r = rounds(h);
        cancel.store(true);  // user hits Esc while the tool runs
        return r;
      },
      tools, gate, cb, /*max_iters=*/8, &cancel);

  CHECK(res.canceled);
  CHECK(round_calls == 1);  // the second request is skipped by the cancel check
}
