#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/HttpClient.hpp"

namespace llmcli {

namespace mcp {
class McpClient;  // mcp/McpClient.hpp
}

// Outcome of running a tool. `output` is the text fed back to the model as the
// tool result; `ok` is false when the tool failed (the model still sees the
// output, which then describes the error).
struct ToolResult {
  bool ok = true;
  std::string output;
};

// A capability the model can invoke. Each tool advertises an OpenAI function
// schema and knows whether it needs user confirmation before running (T11).
class Tool {
 public:
  virtual ~Tool() = default;

  virtual std::string name() const = 0;

  // The full tool definition object: {"type":"function","function":{...}},
  // ready to drop into the request's "tools" array.
  virtual nlohmann::json schema() const = 0;

  // Execute with the parsed JSON arguments object.
  virtual ToolResult execute(const nlohmann::json& args) const = 0;

  // Destructive tools (write_file, run_bash) gate on user confirmation.
  virtual bool requires_confirmation() const = 0;

  // Summary of this call shown in the confirmation prompt. The first line must
  // stay a stable summary: the allow/deny policy prefix-matches on it. Editing
  // tools override this to append a unified diff. Default: command, else path,
  // else raw arguments.
  virtual std::string confirm_details(const nlohmann::json& args) const;
};

// Reads a file and returns its contents.
class ReadFileTool : public Tool {
 public:
  std::string name() const override { return "read_file"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return false; }
};

// Creates or overwrites a file with the given content.
class WriteFileTool : public Tool {
 public:
  std::string name() const override { return "write_file"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return true; }
  // Previews the write as a diff against the file's current contents.
  std::string confirm_details(const nlohmann::json& args) const override;
};

// Runs a shell command, capturing combined stdout/stderr and the exit code.
class RunBashTool : public Tool {
 public:
  std::string name() const override { return "run_bash"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return true; }
};

// Lists the entries of a directory.
class ListDirTool : public Tool {
 public:
  std::string name() const override { return "list_dir"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return false; }
};

// Searches files under a path for a regular expression.
class GrepSearchTool : public Tool {
 public:
  std::string name() const override { return "grep_search"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return false; }
};

// Replaces an exact substring in a file (surgical edit).
class StrReplaceTool : public Tool {
 public:
  std::string name() const override { return "str_replace"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return true; }
  // Previews the edit as a diff (mirrors execute's find/replace semantics).
  std::string confirm_details(const nlohmann::json& args) const override;
};

// Creates a directory (and any missing parents).
class MakeDirTool : public Tool {
 public:
  std::string name() const override { return "make_dir"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return true; }
};

// Fetches a URL over HTTP(S) and returns its readable text (HTML stripped).
class FetchUrlTool : public Tool {
 public:
  // `http` defaults to the real libcurl-backed http_request; tests inject a stub.
  explicit FetchUrlTool(HttpFn http = {});
  std::string name() const override { return "fetch_url"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return false; }

 private:
  HttpFn http_;
};

// Settings for web_search: which backend and where to reach it. When `enabled`
// is false the web tools are not registered at all (see default_tools).
struct WebToolsConfig {
  bool enabled = false;
  std::string search_url;                 // backend search endpoint
  std::string search_api_key;             // backend API key, if required
  std::string search_backend = "searxng"; // searxng | tavily | brave
};

// Runs a query against the configured search backend and returns a ranked list.
class WebSearchTool : public Tool {
 public:
  explicit WebSearchTool(WebToolsConfig cfg, HttpFn http = {});
  std::string name() const override { return "web_search"; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return false; }

 private:
  WebToolsConfig cfg_;
  HttpFn http_;
};

// One web search hit.
struct SearchResult {
  std::string title;
  std::string url;
  std::string snippet;
};

// Pure mapping from a backend's JSON response to a list of results. Tolerant of
// missing fields; results without a URL are dropped. Kept out of the I/O path so
// each backend's response shape is unit-testable.
std::vector<SearchResult> parse_search_results(const std::string& backend,
                                               const nlohmann::json& j);

// Wraps one tool from an MCP server: schema() exposes the server's inputSchema,
// execute() issues a tools/call. Confirm-required (it runs third-party code).
// The shared_ptr keeps the client and its process alive while any tool lives.
class McpTool : public Tool {
 public:
  McpTool(std::shared_ptr<mcp::McpClient> client, std::string exposed_name,
          std::string remote_name, std::string description,
          nlohmann::json input_schema);
  std::string name() const override { return exposed_name_; }
  nlohmann::json schema() const override;
  ToolResult execute(const nlohmann::json& args) const override;
  bool requires_confirmation() const override { return true; }

 private:
  std::shared_ptr<mcp::McpClient> client_;
  std::string exposed_name_;   // name the model sees (may be server-namespaced)
  std::string remote_name_;    // name the server expects in tools/call
  std::string description_;
  nlohmann::json input_schema_;
};

// The built-in tools. When `web.enabled` is true, fetch_url and web_search are
// appended.
std::vector<std::unique_ptr<Tool>> default_tools(const WebToolsConfig& web = {});

}  // namespace llmcli
