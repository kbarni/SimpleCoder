#include "agent/Tool.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using llmcli::default_tools;
using llmcli::ReadFileTool;
using llmcli::RunBashTool;
using llmcli::WriteFileTool;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path temp_path(const std::string& tag) {
  return fs::temp_directory_path() /
         ("llm_cli_tool_" + tag + "_" + std::to_string(::getpid()));
}

}  // namespace

TEST_CASE("read_file returns file contents", "[tools]") {
  const auto p = temp_path("read");
  {
    std::ofstream(p) << "hello tools";
  }

  ReadFileTool tool;
  auto res = tool.execute({{"path", p.string()}});
  CHECK(res.ok);
  CHECK(res.output == "hello tools");

  fs::remove(p);
}

TEST_CASE("read_file reports a missing file as an error", "[tools]") {
  ReadFileTool tool;
  auto res = tool.execute({{"path", "/no/such/file/here"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("error") != std::string::npos);
}

TEST_CASE("read_file requires a path", "[tools]") {
  ReadFileTool tool;
  auto res = tool.execute(json::object());
  CHECK_FALSE(res.ok);
}

TEST_CASE("write_file creates and overwrites", "[tools]") {
  const auto p = temp_path("write");
  fs::remove(p);

  WriteFileTool tool;
  auto res = tool.execute({{"path", p.string()}, {"content", "first"}});
  REQUIRE(res.ok);
  CHECK(fs::exists(p));

  auto read_back = [&] {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), {});
  };
  CHECK(read_back() == "first");

  // Overwrite, not append.
  tool.execute({{"path", p.string()}, {"content", "second"}});
  CHECK(read_back() == "second");

  fs::remove(p);
}

TEST_CASE("run_bash captures stdout and a zero exit", "[tools]") {
  RunBashTool tool;
  auto res = tool.execute({{"command", "echo hello"}});
  CHECK(res.ok);
  CHECK(res.output.find("hello") != std::string::npos);
}

TEST_CASE("run_bash captures stderr and a non-zero exit", "[tools]") {
  RunBashTool tool;
  auto res = tool.execute({{"command", "echo oops >&2; exit 3"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("oops") != std::string::npos);
  CHECK(res.output.find("exit code 3") != std::string::npos);
}

TEST_CASE("confirmation flags match the safety policy", "[tools]") {
  CHECK_FALSE(ReadFileTool{}.requires_confirmation());
  CHECK(WriteFileTool{}.requires_confirmation());
  CHECK(RunBashTool{}.requires_confirmation());
}

TEST_CASE("schemas advertise name and required params", "[tools]") {
  ReadFileTool read;
  auto s = read.schema();
  CHECK(s["type"] == "function");
  CHECK(s["function"]["name"] == "read_file");
  CHECK(s["function"]["parameters"]["required"][0] == "path");

  WriteFileTool write;
  auto ws = write.schema()["function"]["parameters"]["required"];
  CHECK(ws.size() == 2);  // path + content
}

TEST_CASE("default_tools returns the built-in set", "[tools]") {
  auto tools = default_tools();
  REQUIRE(tools.size() == 7);
  CHECK(tools[0]->name() == "read_file");
  CHECK(tools[1]->name() == "write_file");
  CHECK(tools[2]->name() == "run_bash");
  CHECK(tools[3]->name() == "list_dir");
  CHECK(tools[4]->name() == "grep_search");
  CHECK(tools[5]->name() == "str_replace");
  CHECK(tools[6]->name() == "make_dir");
}

TEST_CASE("list_dir lists entries and flags directories", "[tools]") {
  const auto root = temp_path("ls");
  fs::remove_all(root);
  fs::create_directories(root / "sub");
  { std::ofstream(root / "a.txt") << "hi"; }

  llmcli::ListDirTool tool;
  auto res = tool.execute({{"path", root.string()}});
  REQUIRE(res.ok);
  CHECK(res.output.find("a.txt") != std::string::npos);
  CHECK(res.output.find("[dir]") != std::string::npos);
  CHECK(res.output.find("sub") != std::string::npos);

  auto missing = tool.execute({{"path", (root / "nope").string()}});
  CHECK_FALSE(missing.ok);

  fs::remove_all(root);
}

TEST_CASE("grep_search finds matching lines with file:line", "[tools]") {
  const auto root = temp_path("grep");
  fs::remove_all(root);
  fs::create_directories(root);
  { std::ofstream(root / "f.txt") << "alpha\nNEEDLE here\nbeta\n"; }

  llmcli::GrepSearchTool tool;
  auto res = tool.execute({{"pattern", "NEEDLE"}, {"path", root.string()}});
  REQUIRE(res.ok);
  CHECK(res.output.find("f.txt:2:") != std::string::npos);
  CHECK(res.output.find("NEEDLE here") != std::string::npos);

  auto none = tool.execute({{"pattern", "zzz"}, {"path", root.string()}});
  CHECK(none.ok);
  CHECK(none.output.find("no matches") != std::string::npos);

  auto bad = tool.execute({{"pattern", "("}, {"path", root.string()}});
  CHECK_FALSE(bad.ok);  // invalid regex

  fs::remove_all(root);
}

TEST_CASE("str_replace edits a unique substring", "[tools]") {
  const auto p = temp_path("strrep");
  { std::ofstream(p) << "hello world"; }

  auto read_back = [&] {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), {});
  };

  llmcli::StrReplaceTool tool;
  auto res =
      tool.execute({{"path", p.string()}, {"old_str", "world"}, {"new_str", "there"}});
  REQUIRE(res.ok);
  CHECK(read_back() == "hello there");

  // Not found.
  auto nf = tool.execute(
      {{"path", p.string()}, {"old_str", "absent"}, {"new_str", "x"}});
  CHECK_FALSE(nf.ok);

  fs::remove(p);
}

TEST_CASE("str_replace refuses an ambiguous match", "[tools]") {
  const auto p = temp_path("strrep2");
  { std::ofstream(p) << "x and x"; }

  llmcli::StrReplaceTool tool;
  auto res =
      tool.execute({{"path", p.string()}, {"old_str", "x"}, {"new_str", "y"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("not unique") != std::string::npos);

  fs::remove(p);
}

TEST_CASE("make_dir creates nested directories", "[tools]") {
  const auto root = temp_path("mkdir");
  fs::remove_all(root);

  llmcli::MakeDirTool tool;
  auto res = tool.execute({{"path", (root / "a" / "b").string()}});
  REQUIRE(res.ok);
  CHECK(fs::is_directory(root / "a" / "b"));

  fs::remove_all(root);
}

TEST_CASE("new tools have the expected confirmation policy", "[tools]") {
  CHECK_FALSE(llmcli::ListDirTool{}.requires_confirmation());
  CHECK_FALSE(llmcli::GrepSearchTool{}.requires_confirmation());
  CHECK(llmcli::StrReplaceTool{}.requires_confirmation());
  CHECK(llmcli::MakeDirTool{}.requires_confirmation());
}

// --- confirm_details: diff preview -----------------------------------------

TEST_CASE("write_file confirm_details previews a new file as additions",
          "[tools][confirm]") {
  const auto p = temp_path("wf_new");
  fs::remove(p);
  WriteFileTool tool;
  const std::string d =
      tool.confirm_details({{"path", p.string()}, {"content", "alpha\nbeta\n"}});
  CHECK(d.rfind("path: ", 0) == 0);                 // stable summary first
  CHECK(d.find("new file") != std::string::npos);
  CHECK(d.find("+alpha") != std::string::npos);
  CHECK(d.find("+beta") != std::string::npos);
}

TEST_CASE("write_file confirm_details diffs against existing content",
          "[tools][confirm]") {
  const auto p = temp_path("wf_over");
  { std::ofstream(p) << "a\nb\nc\n"; }
  WriteFileTool tool;
  const std::string d =
      tool.confirm_details({{"path", p.string()}, {"content", "a\nB\nc\n"}});
  CHECK(d.find("overwrite") != std::string::npos);
  CHECK(d.find("-b") != std::string::npos);
  CHECK(d.find("+B") != std::string::npos);
  fs::remove(p);
}

TEST_CASE("write_file confirm_details reports no changes", "[tools][confirm]") {
  const auto p = temp_path("wf_same");
  { std::ofstream(p) << "same\n"; }
  WriteFileTool tool;
  const std::string d =
      tool.confirm_details({{"path", p.string()}, {"content", "same\n"}});
  CHECK(d.find("(no changes)") != std::string::npos);
  fs::remove(p);
}

TEST_CASE("str_replace confirm_details previews the edit", "[tools][confirm]") {
  const auto p = temp_path("sr_ok");
  { std::ofstream(p) << "int x = 1;\nint y = 2;\n"; }
  llmcli::StrReplaceTool tool;
  const std::string d = tool.confirm_details(
      {{"path", p.string()}, {"old_str", "x = 1"}, {"new_str", "x = 42"}});
  CHECK(d.find("-int x = 1;") != std::string::npos);
  CHECK(d.find("+int x = 42;") != std::string::npos);
  fs::remove(p);
}

TEST_CASE("str_replace confirm_details flags a failing edit",
          "[tools][confirm]") {
  const auto p = temp_path("sr_miss");
  { std::ofstream(p) << "hello\n"; }
  llmcli::StrReplaceTool tool;
  const std::string d = tool.confirm_details(
      {{"path", p.string()}, {"old_str", "nope"}, {"new_str", "x"}});
  CHECK(d.find("not found") != std::string::npos);
  fs::remove(p);
}

TEST_CASE("base confirm_details summarizes command and path",
          "[tools][confirm]") {
  CHECK(RunBashTool{}.confirm_details({{"command", "git status"}}) ==
        "git status");
  CHECK(llmcli::MakeDirTool{}.confirm_details({{"path", "build/out"}}) ==
        "path: build/out");
}
