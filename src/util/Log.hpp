#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace llmcli {

enum class LogLevel { Debug, Info, Warn, Error };

// A minimal thread-safe file logger. ncurses owns the terminal, so diagnostics
// must go to a file rather than stdout/stderr. A default-constructed Logger has
// no sink and silently discards messages.
class Logger {
 public:
  Logger() = default;
  explicit Logger(const std::filesystem::path& file,
                  LogLevel min_level = LogLevel::Info);

  // (Re)point the logger at a file. Used to initialize the global logger.
  void open(const std::filesystem::path& file, LogLevel min_level);

  void set_level(LogLevel level) { min_level_ = level; }
  bool enabled() const { return out_.is_open(); }

  void log(LogLevel level, std::string_view msg);

  void debug(std::string_view m) { log(LogLevel::Debug, m); }
  void info(std::string_view m) { log(LogLevel::Info, m); }
  void warn(std::string_view m) { log(LogLevel::Warn, m); }
  void error(std::string_view m) { log(LogLevel::Error, m); }

 private:
  std::mutex mutex_;
  std::ofstream out_;
  LogLevel min_level_ = LogLevel::Info;
};

// Process-global logger. Inert until init_logging() is called.
Logger& log();
void init_logging(const std::filesystem::path& file,
                  LogLevel min_level = LogLevel::Info);

}  // namespace llmcli
