#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "llm/Message.hpp"

namespace llmcli {

// Reads a file's contents (binary-safe); returns nullopt if it can't be read.
// Injectable so the expander is unit-testable without touching the filesystem.
using FileReader = std::function<std::optional<std::string>(const std::string&)>;

struct ExpandResult {
  std::string text;                  // message to send (use only if errors empty)
  std::vector<std::string> attached; // text paths inlined, in order
  std::vector<ImagePart> images;     // image attachments (base64 data URLs)
  std::vector<std::string> errors;   // human-readable errors for unreadable paths
};

// True if `path` has a known raster-image extension (case-insensitive).
bool is_image_path(std::string_view path);

// Guess an `image/*` MIME type from a path's extension (defaults to
// "application/octet-stream" for unknown extensions).
std::string image_media_type(std::string_view path);

// Expand `@path` tokens in a typed line. Text files are inlined as a fenced
// block appended after the original text; image files (by extension) are
// base64-encoded into an `image_url` data URL and returned in `images` (the
// `@token` stays in the text so the model sees the filename). A token is an `@`
// at the start of the line or after whitespace, followed by a non-space path;
// this leaves bare `@`, and `@` inside a word (e.g. an email address), untouched.
//
// An image larger than `max_image_bytes` is recorded in `errors` and skipped
// (rather than truncated); `0` disables the cap. If any referenced file can't be
// read, the path is recorded in `errors` (the caller should decline to send).
// Lines with no tokens pass through unchanged.
ExpandResult expand_attachments(std::string_view line, const FileReader& reader,
                                std::size_t max_image_bytes = 0);

}  // namespace llmcli
