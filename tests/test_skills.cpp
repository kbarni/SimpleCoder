#include "app/Skill.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using llmcli::loadSkills;
using llmcli::parse_skill;
using llmcli::Skill;
using llmcli::SkillRegistry;
namespace fs = std::filesystem;

// --- parse_skill -----------------------------------------------------------

TEST_CASE("parse_skill reads frontmatter and body", "[skills]") {
  const Skill s = parse_skill(
      "---\n"
      "name: review-diff\n"
      "description: Review the current diff.\n"
      "---\n"
      "Run git diff and look for bugs.\n");
  CHECK(s.name == "review-diff");
  CHECK(s.description == "Review the current diff.");
  CHECK(s.body == "Run git diff and look for bugs.");
}

TEST_CASE("parse_skill without frontmatter is all body", "[skills]") {
  const Skill s = parse_skill("Just do the thing.\nSecond line.");
  CHECK(s.name.empty());
  CHECK(s.description.empty());
  CHECK(s.body == "Just do the thing.\nSecond line.");
}

TEST_CASE("parse_skill strips quotes and ignores unknown keys", "[skills]") {
  const Skill s = parse_skill(
      "---\n"
      "name: \"my skill\"\n"
      "trigger: whatever\n"
      "description: 'quoted desc'\n"
      "---\n"
      "body text");
  CHECK(s.name == "my skill");
  CHECK(s.description == "quoted desc");
  CHECK(s.body == "body text");
}

TEST_CASE("parse_skill treats unterminated frontmatter as all body",
          "[skills]") {
  const Skill s = parse_skill("---\nname: x\nbody with no close fence");
  // No closing fence: frontmatter is malformed, so nothing is swallowed.
  CHECK(s.name.empty());
  CHECK(s.body == "---\nname: x\nbody with no close fence");
}

// --- SkillRegistry ---------------------------------------------------------

TEST_CASE("registry lookup is case-insensitive and first-wins", "[skills]") {
  SkillRegistry reg;
  reg.add({"Build", "from project", "project body"});
  reg.add({"build", "from user", "user body"});  // duplicate name: ignored

  REQUIRE(reg.all().size() == 1);
  const Skill* s = reg.find("BUILD");
  REQUIRE(s != nullptr);
  CHECK(s->body == "project body");
  CHECK(reg.find("missing") == nullptr);
}

// --- loadSkills ------------------------------------------------------------

namespace {
fs::path make_temp_dir(const std::string& tag) {
  const fs::path d = fs::temp_directory_path() /
                     ("llm_cli_skills_" + tag + "_" + std::to_string(::getpid()));
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
void write_file(const fs::path& p, const std::string& content) {
  std::ofstream(p) << content;
}
}  // namespace

TEST_CASE("loadSkills reads *.md and names from the file stem", "[skills]") {
  const fs::path dir = make_temp_dir("load");
  write_file(dir / "commit.md", "Write a commit message.");  // no frontmatter
  write_file(dir / "named.md",
             "---\nname: custom\n---\nbody");  // frontmatter name wins
  write_file(dir / "notes.txt", "ignored, not markdown");

  const SkillRegistry reg = loadSkills({dir});
  CHECK(reg.all().size() == 2);
  REQUIRE(reg.find("commit") != nullptr);          // from the file stem
  CHECK(reg.find("commit")->body == "Write a commit message.");
  CHECK(reg.find("custom") != nullptr);            // from frontmatter
  CHECK(reg.find("named") == nullptr);             // stem overridden by name

  fs::remove_all(dir);
}

TEST_CASE("loadSkills lets earlier directories win", "[skills]") {
  const fs::path proj = make_temp_dir("proj");
  const fs::path user = make_temp_dir("user");
  write_file(proj / "deploy.md", "project deploy");
  write_file(user / "deploy.md", "user deploy");
  write_file(user / "extra.md", "only in user");

  const SkillRegistry reg = loadSkills({proj, user});
  REQUIRE(reg.find("deploy") != nullptr);
  CHECK(reg.find("deploy")->body == "project deploy");  // project precedence
  CHECK(reg.find("extra") != nullptr);                  // user-only still loads

  fs::remove_all(proj);
  fs::remove_all(user);
}

TEST_CASE("loadSkills skips empty-bodied files and missing dirs", "[skills]") {
  const fs::path dir = make_temp_dir("empty");
  write_file(dir / "blank.md", "---\nname: blank\n---\n   \n");

  const SkillRegistry reg = loadSkills({dir, "/no/such/skills/dir"});
  CHECK(reg.empty());

  fs::remove_all(dir);
}
