#include "mcp/Transport.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "util/Log.hpp"

namespace llmcli::mcp {

using nlohmann::json;

namespace {

// Max wait for one response, so a wedged server can't hang the UI thread.
constexpr int kReadTimeoutMs = 30000;

// A child process speaking newline-delimited JSON-RPC over stdin/stdout. The
// client is synchronous, so request() writes a line and reads until it sees the
// matching id.
class StdioTransport : public Transport {
 public:
  StdioTransport(pid_t pid, int to_child, int from_child)
      : pid_(pid), to_child_(to_child), from_child_(from_child) {}

  ~StdioTransport() override {
    if (to_child_ >= 0) ::close(to_child_);    // EOF on stdin asks it to exit
    if (from_child_ >= 0) ::close(from_child_);
    if (pid_ > 0) {
      // Give it a moment to exit cleanly, then make sure it's gone.
      for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t r = ::waitpid(pid_, &status, WNOHANG);
        if (r == pid_ || (r < 0 && errno == ECHILD)) return;
        ::usleep(5000);
      }
      ::kill(pid_, SIGTERM);
      ::waitpid(pid_, nullptr, 0);
    }
  }

  RpcResult request(const json& req) override {
    if (!write_line(req)) {
      return {false, {}, "mcp: write to server failed"};
    }
    const auto want = req.find("id");
    for (;;) {
      auto msg = read_line();
      if (!msg) {
        return {false, {}, last_error_.empty() ? "mcp: server closed the connection"
                                               : last_error_};
      }
      // Skip notifications / responses to other ids; return our match.
      if (want != req.end()) {
        auto got = msg->find("id");
        if (got != msg->end() && *got == *want) {
          return {true, std::move(*msg), {}};
        }
        continue;  // not ours — keep reading
      }
      return {true, std::move(*msg), {}};
    }
  }

  bool notify(const json& note) override { return write_line(note); }

 private:
  bool write_line(const json& msg) {
    std::string line = msg.dump();
    line += '\n';
    std::size_t off = 0;
    while (off < line.size()) {
      ssize_t n = ::write(to_child_, line.data() + off, line.size() - off);
      if (n < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      off += static_cast<std::size_t>(n);
    }
    return true;
  }

  // Read one '\n'-terminated JSON message from the child, with a timeout. Lines
  // that don't parse as JSON are skipped (some servers print stray output).
  std::optional<json> read_line() {
    for (;;) {
      if (auto nl = buf_.find('\n'); nl != std::string::npos) {
        std::string line = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) continue;  // not JSON — skip this line
        return j;
      }

      pollfd pfd{from_child_, POLLIN, 0};
      int pr = ::poll(&pfd, 1, kReadTimeoutMs);
      if (pr == 0) {
        last_error_ = "mcp: timed out waiting for the server";
        return std::nullopt;
      }
      if (pr < 0) {
        if (errno == EINTR) continue;
        last_error_ = std::string("mcp: poll failed: ") + std::strerror(errno);
        return std::nullopt;
      }

      char chunk[4096];
      ssize_t n = ::read(from_child_, chunk, sizeof(chunk));
      if (n < 0) {
        if (errno == EINTR) continue;
        last_error_ = std::string("mcp: read failed: ") + std::strerror(errno);
        return std::nullopt;
      }
      if (n == 0) {  // EOF: flush any final unterminated line
        if (!buf_.empty()) {
          json j = json::parse(buf_, nullptr, false);
          buf_.clear();
          if (!j.is_discarded()) return j;
        }
        return std::nullopt;
      }
      buf_.append(chunk, static_cast<std::size_t>(n));
    }
  }

  pid_t pid_;
  int to_child_;
  int from_child_;
  std::string buf_;
  std::string last_error_;
};

}  // namespace

TransportPtr open_stdio_transport(const McpServerConfig& cfg) {
  int in_pipe[2];   // parent writes -> child stdin
  int out_pipe[2];  // child stdout -> parent reads
  if (::pipe(in_pipe) != 0) return nullptr;
  if (::pipe(out_pipe) != 0) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    return nullptr;
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    return nullptr;
  }

  if (pid == 0) {
    // Child: wire pipes to stdin/stdout, silence stderr, then exec.
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    if (int devnull = ::open("/dev/null", O_WRONLY); devnull >= 0) {
      ::dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    }
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);

    for (const auto& [k, v] : cfg.env) ::setenv(k.c_str(), v.c_str(), 1);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(cfg.command.c_str()));
    for (const auto& a : cfg.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    ::execvp(cfg.command.c_str(), argv.data());
    ::_exit(127);  // exec failed
  }

  // Parent: keep the ends we use, close the child's.
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  ::llmcli::log().info("mcp: started server '" + cfg.name + "' (" + cfg.command +
                       ")");
  return std::make_unique<StdioTransport>(pid, in_pipe[1], out_pipe[0]);
}

}  // namespace llmcli::mcp
