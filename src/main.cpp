#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <ncursesw/ncurses.h>

#include "app/App.hpp"
#include "app/Config.hpp"
#include "app/SystemPrompt.hpp"
#include "ui/ChatView.hpp"
#include "ui/InputBar.hpp"
#include "ui/Tui.hpp"

namespace {

void print_version() {
  std::cout << "</>SimpleCoder " << SIMPLECODER_VERSION << "\n";
  std::cout << "config: " << llmcli::userConfigPath().string() << "\n";
}

void print_help() {
  std::cout << "Usage: SimpleCoder [OPTIONS] [CONFIG_FILE]\n\n"
               "Arguments:\n"
               "  CONFIG_FILE      Extra config file, merged on top of the\n"
               "                   default config layers\n\n"
               "Options:\n"
               "  -v, --version    Print version and config path\n"
               "  -h, --help       Show this help\n";
}

}  // namespace

int main(int argc, char** argv) {
  const char* config_file = "";  // optional extra config file (positional arg)
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-v" || arg == "--version") {
      print_version();
      return 0;
    } else if (arg == "-h" || arg == "--help") {
      print_help();
      return 0;
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "SimpleCoder: unknown option '" << arg << "'\n";
      print_help();
      return 2;
    } else {
      // A bare path: an extra config file merged on top of the usual layers.
      config_file = argv[i];
    }
  }

  // An explicitly requested config file that doesn't exist is almost always a
  // typo — fail loudly rather than silently ignoring it.
  if (*config_file && !std::filesystem::exists(config_file)) {
    std::cerr << "SimpleCoder: config file not found: " << config_file << "\n";
    return 2;
  }

  // Default: launch the interactive chat against the configured server.
  try {
    llmcli::Config cfg = llmcli::loadConfig(config_file);
    // An AGENTS.md in the project (or beside the binary) overrides the
    // configured system prompt.
    cfg.system_prompt = llmcli::resolveSystemPrompt(cfg.system_prompt);
    llmcli::App app(std::move(cfg));
    return app.run();
  } catch (const llmcli::ConfigError& e) {
    namespace fs = std::filesystem;
    const fs::path user = llmcli::userConfigPath();
    const fs::path bin = llmcli::binaryDirConfigPath();
    const fs::path local = llmcli::localConfigPath();
    std::error_code ec;
    const bool any = fs::exists(user, ec) ||
                     (!bin.empty() && fs::exists(bin, ec)) ||
                     fs::exists(local, ec);

    std::cerr << e.what() << "\n";
    if (!any) {
      // Fresh machine: drop a commented starter config so there's something to
      // edit. Prefer next to the binary; fall back to the working directory.
      const fs::path written =
          llmcli::writeStarterConfig(bin.empty() ? local : bin, local);
      if (!written.empty())
        std::cerr << "Created a starter config at " << written.string()
                  << " — edit base_url and run again.\n";
      else
        std::cerr << "Set OPENAI_BASE_URL (and MODEL), or create "
                  << user.string() << " — see config.example.conf.\n";
    } else {
      std::cerr << "Set OPENAI_BASE_URL (and MODEL), or edit your config.conf"
                   " — see config.example.conf.\n";
    }
    return 1;
  }
}
