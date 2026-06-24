#pragma once

#include <string>
#include <string_view>

namespace llmcli {

// Standard base64 (RFC 4648). `bytes` may be arbitrary binary; the result is ASCII.
std::string base64_encode(std::string_view bytes);

// Inverse of base64_encode. Skips whitespace; stops at the first invalid
// character (best-effort, never throws). Used mainly for round-trip tests.
std::string base64_decode(std::string_view b64);

}  // namespace llmcli
