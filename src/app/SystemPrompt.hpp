#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/Attachments.hpp"  // FileReader

namespace llmcli {

// AGENTS.md search locations, in priority order: the current working directory,
// then next to the binary.
std::vector<std::filesystem::path> agentsMdPaths();

// Effective system prompt: the contents of the first existing, non-empty
// AGENTS.md candidate (read via `reader`), otherwise `configPrompt`. Pure and
// unit-testable.
std::string resolveSystemPrompt(
    std::string_view configPrompt,
    const std::vector<std::filesystem::path>& candidates,
    const FileReader& reader);

// Convenience: resolve against agentsMdPaths(), reading from disk.
std::string resolveSystemPrompt(std::string_view configPrompt);

// The prompt sent to the model by /init to generate an AGENTS.md, given a
// listing of the project's files.
std::string initPrompt(std::string_view file_listing);

// The prompt sent to the model by /compact, asking it to summarize the
// conversation so the summary can replace the full history as context.
std::string compactPrompt();

// If `s` is wrapped in a ``` code fence, return its inner contents; otherwise
// return `s` unchanged. Used to clean up /init output.
std::string stripCodeFence(std::string s);

}  // namespace llmcli
