#include "util/Log.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using llmcli::Logger;
using llmcli::LogLevel;

namespace {

std::string read_file(const std::filesystem::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::filesystem::path temp_log() {
  return std::filesystem::temp_directory_path() /
         ("llm_cli_log_test_" + std::to_string(::getpid()) + ".log");
}

}  // namespace

TEST_CASE("logger writes messages above the threshold", "[log]") {
  const auto path = temp_log();
  std::filesystem::remove(path);

  {
    Logger logger(path, LogLevel::Info);
    logger.debug("hidden");   // below threshold -> dropped
    logger.info("visible info");
    logger.error("visible error");
  }

  const std::string contents = read_file(path);
  CHECK(contents.find("hidden") == std::string::npos);
  CHECK(contents.find("visible info") != std::string::npos);
  CHECK(contents.find("[INFO]") != std::string::npos);
  CHECK(contents.find("[ERROR]") != std::string::npos);

  std::filesystem::remove(path);
}

TEST_CASE("default-constructed logger discards silently", "[log]") {
  Logger logger;  // no sink
  CHECK_FALSE(logger.enabled());
  logger.error("goes nowhere");  // must not crash
}
