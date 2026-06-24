#include <catch2/catch_test_macros.hpp>

#include "app/Command.hpp"

using namespace llmcli;

TEST_CASE("a plain line is not a command", "[command]") {
  auto c = parse_command("hello there");
  CHECK(c.kind == CommandKind::None);
}

TEST_CASE("recognised commands parse to their kind", "[command]") {
  CHECK(parse_command("/help").kind == CommandKind::Help);
  CHECK(parse_command("/?").kind == CommandKind::Help);
  CHECK(parse_command("/clear").kind == CommandKind::Clear);
  CHECK(parse_command("/retry").kind == CommandKind::Retry);
  CHECK(parse_command("/init").kind == CommandKind::Init);
  CHECK(parse_command("/compact").kind == CommandKind::Compact);
  CHECK(parse_command("/skill").kind == CommandKind::Skill);
  CHECK(parse_command("/quit").kind == CommandKind::Quit);
  CHECK(parse_command("/exit").kind == CommandKind::Quit);
}

TEST_CASE("/skill captures the rest as its argument", "[command]") {
  auto list = parse_command("/skill");
  CHECK(list.kind == CommandKind::Skill);
  CHECK(list.arg.empty());

  auto run = parse_command("/skill review-diff please be terse");
  CHECK(run.kind == CommandKind::Skill);
  CHECK(run.arg == "review-diff please be terse");

  CHECK(parse_command("/skills").kind == CommandKind::Skill);  // alias
}

TEST_CASE("commands are case-insensitive and whitespace-tolerant",
          "[command]") {
  CHECK(parse_command("  /HELP  ").kind == CommandKind::Help);
  CHECK(parse_command("/Quit").kind == CommandKind::Quit);
}

TEST_CASE("/model captures its argument", "[command]") {
  auto c = parse_command("/model qwen2.5-coder:7b");
  CHECK(c.kind == CommandKind::Model);
  CHECK(c.arg == "qwen2.5-coder:7b");

  auto bare = parse_command("/model");
  CHECK(bare.kind == CommandKind::Model);
  CHECK(bare.arg.empty());

  auto spaced = parse_command("/model   llama3  ");
  CHECK(spaced.arg == "llama3");
}

TEST_CASE("an unrecognised slash word is Unknown with the word", "[command]") {
  auto c = parse_command("/frobnicate now");
  CHECK(c.kind == CommandKind::Unknown);
  CHECK(c.arg == "frobnicate");
}

TEST_CASE("a lone slash is not a recognised command", "[command]") {
  // "/" with no word -> Unknown with empty word (handled as an error by App).
  auto c = parse_command("/");
  CHECK(c.kind == CommandKind::Unknown);
}

TEST_CASE("help text lists commands and keys", "[command]") {
  auto lines = help_lines();
  bool has_model = false, has_keys = false;
  for (const auto& l : lines) {
    if (l.find("/model") != std::string::npos) has_model = true;
    if (l.find("F1") != std::string::npos) has_keys = true;
  }
  CHECK(has_model);
  CHECK(has_keys);
}
