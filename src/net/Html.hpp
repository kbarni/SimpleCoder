#pragma once

#include <string>
#include <string_view>

namespace llmcli {

// Convert an HTTP response body to readable plain text. HTML is stripped of
// tags (with <script>/<style> contents dropped, block elements turned into line
// breaks) and common entities are decoded; whitespace is collapsed. Bodies that
// aren't HTML (per `content_type`, e.g. JSON or plain text) pass through
// unchanged. `content_type` may be empty, in which case the body is sniffed.
std::string html_to_text(std::string_view body, std::string_view content_type);

}  // namespace llmcli
