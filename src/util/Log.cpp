#include "util/Log.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>

namespace llmcli {

namespace {

std::string_view level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

}  // namespace

Logger::Logger(const std::filesystem::path& file, LogLevel min_level)
    : out_(file, std::ios::app), min_level_(min_level) {}

void Logger::open(const std::filesystem::path& file, LogLevel min_level) {
  std::lock_guard<std::mutex> lock(mutex_);
  out_ = std::ofstream(file, std::ios::app);
  min_level_ = min_level;
}

void Logger::log(LogLevel level, std::string_view msg) {
  if (level < min_level_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!out_.is_open()) return;
  out_ << timestamp() << " [" << level_name(level) << "] " << msg << '\n';
  out_.flush();
}

Logger& log() {
  static Logger instance;
  return instance;
}

void init_logging(const std::filesystem::path& file, LogLevel min_level) {
  log().open(file, min_level);
}

}  // namespace llmcli
