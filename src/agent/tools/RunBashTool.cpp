#include "agent/Tool.hpp"

#include <sys/wait.h>

#include <array>
#include <cstdio>

namespace llmcli {

using nlohmann::json;

json RunBashTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Run a shell command and return its combined stdout/stderr and exit "
         "code."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"command",
             {{"type", "string"},
              {"description", "The shell command to execute."}}}}},
          {"required", json::array({"command"})}}}}}};
}

ToolResult RunBashTool::execute(const json& args) const {
  const std::string command = args.value("command", "");
  if (command.empty()) {
    return {false, "error: 'command' argument is required"};
  }

  // Run in a subshell so the stderr->stdout redirect applies to the whole
  // command (not just its last statement), letting the model see diagnostics.
  const std::string full = "( " + command + " ) 2>&1";
  FILE* pipe = popen(full.c_str(), "r");
  if (!pipe) {
    return {false, "error: failed to start command"};
  }

  std::string out;
  std::array<char, 4096> buf{};
  std::size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
    out.append(buf.data(), n);
  }

  const int status = pclose(pipe);
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  ToolResult result;
  result.ok = (code == 0);
  result.output = out;
  if (code != 0) {
    if (!result.output.empty() && result.output.back() != '\n') {
      result.output += '\n';
    }
    result.output += "[exit code " + std::to_string(code) + "]";
  }
  return result;
}

}  // namespace llmcli
