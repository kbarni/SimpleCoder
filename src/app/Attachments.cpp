#include "app/Attachments.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "util/Base64.hpp"

namespace llmcli {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// Lowercased file extension including the dot (e.g. ".png"), or "" if none.
std::string lower_ext(std::string_view path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

// Collect the `@path` tokens, in order, that qualify as attachments.
std::vector<std::string> find_paths(std::string_view line) {
  std::vector<std::string> paths;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] != '@') continue;
    const bool at_boundary = (i == 0) || is_space(line[i - 1]);
    if (!at_boundary) continue;  // e.g. an email address: name@host
    std::size_t j = i + 1;
    while (j < line.size() && !is_space(line[j])) ++j;
    if (j > i + 1) paths.emplace_back(line.substr(i + 1, j - i - 1));
    i = j;  // skip past the token
  }
  return paths;
}

}  // namespace

bool is_image_path(std::string_view path) {
  const std::string ext = lower_ext(path);
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" ||
         ext == ".webp" || ext == ".bmp";
}

std::string image_media_type(std::string_view path) {
  const std::string ext = lower_ext(path);
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".webp") return "image/webp";
  if (ext == ".bmp") return "image/bmp";
  return "application/octet-stream";
}

ExpandResult expand_attachments(std::string_view line, const FileReader& reader,
                                std::size_t max_image_bytes) {
  ExpandResult r;
  const std::vector<std::string> paths = find_paths(line);

  std::string body(line);
  for (const auto& path : paths) {
    std::optional<std::string> contents = reader(path);
    if (!contents) {
      r.errors.push_back("Cannot read file: " + path);
      continue;
    }
    if (is_image_path(path)) {
      // Reject (don't truncate) an image over the configured cap; base64
      // inflates it ~33% and it re-sends every turn until /compact.
      if (max_image_bytes != 0 && contents->size() > max_image_bytes) {
        r.errors.push_back("Image too large: " + path + " (" +
                           std::to_string(contents->size()) + " > " +
                           std::to_string(max_image_bytes) + " bytes)");
        continue;
      }
      // Image: base64 into a data URL, sent as a separate content part. The
      // `@path` token stays in the text so the model still sees the filename.
      const std::string media = image_media_type(path);
      ImagePart img;
      img.name = std::filesystem::path(path).filename().string();
      img.media_type = media;
      img.bytes = contents->size();
      img.data_url = "data:" + media + ";base64," + base64_encode(*contents);
      r.images.push_back(std::move(img));
      continue;
    }
    body += "\n\nAttached file `" + path + "`:\n```\n" + *contents;
    if (!contents->empty() && contents->back() != '\n') body += '\n';
    body += "```";
    r.attached.push_back(path);
  }

  r.text = std::move(body);
  return r;
}

}  // namespace llmcli
