#include "llm/ChatResponse.hpp"

#include <nlohmann/json.hpp>

namespace llmcli {

using nlohmann::json;

namespace {

std::string_view trim(std::string_view s) {
  const auto ws = " \t\r\n";
  const auto begin = s.find_first_not_of(ws);
  if (begin == std::string_view::npos) return {};
  const auto end = s.find_last_not_of(ws);
  return s.substr(begin, end - begin + 1);
}

}  // namespace

StreamParser::StreamParser(ContentCallback on_content,
                           ContentCallback on_reasoning)
    : on_content_(std::move(on_content)),
      on_reasoning_(std::move(on_reasoning)) {}

void StreamParser::feed(std::string_view bytes) {
  buffer_.append(bytes);

  // Process every complete line (terminated by '\n'); keep the remainder.
  std::size_t pos;
  while ((pos = buffer_.find('\n')) != std::string::npos) {
    process_line(std::string_view(buffer_).substr(0, pos));
    buffer_.erase(0, pos + 1);
  }
}

void StreamParser::process_line(std::string_view raw) {
  const std::string_view line = trim(raw);
  if (line.empty()) return;            // event separator
  if (line.front() == ':') return;     // SSE comment / keep-alive

  constexpr std::string_view kData = "data:";
  if (line.substr(0, kData.size()) != kData) return;  // ignore non-data fields

  const std::string_view payload = trim(line.substr(kData.size()));
  if (payload == "[DONE]") {
    done_ = true;
    return;
  }
  process_chunk(payload);
}

void StreamParser::process_chunk(std::string_view json_payload) {
  json chunk = json::parse(json_payload, nullptr, /*allow_exceptions=*/false);
  if (chunk.is_discarded()) return;  // skip malformed chunks

  // Token usage may arrive as a trailing chunk with an empty `choices` array
  // (the OpenAI stream_options.include_usage shape), so read it first.
  if (auto u = chunk.find("usage"); u != chunk.end() && u->is_object()) {
    prompt_tokens_ = u->value("prompt_tokens", prompt_tokens_);
    completion_tokens_ = u->value("completion_tokens", completion_tokens_);
    total_tokens_ = u->value("total_tokens", total_tokens_);
  }

  if (!chunk.contains("choices") || !chunk["choices"].is_array() ||
      chunk["choices"].empty()) {
    return;  // no per-choice delta in this chunk (e.g. the usage-only chunk)
  }

  const json& choice = chunk["choices"][0];

  if (auto fr = choice.find("finish_reason");
      fr != choice.end() && fr->is_string()) {
    finish_reason_ = fr->get<std::string>();
  }

  const auto delta = choice.find("delta");
  if (delta == choice.end() || !delta->is_object()) return;

  if (auto c = delta->find("content");
      c != delta->end() && c->is_string()) {
    const std::string piece = c->get<std::string>();
    content_ += piece;
    if (on_content_) on_content_(piece);
  }

  if (auto r = delta->find("reasoning_content");
      r != delta->end() && r->is_string()) {
    const std::string piece = r->get<std::string>();
    reasoning_ += piece;
    if (on_reasoning_) on_reasoning_(piece);
  }

  if (auto tcs = delta->find("tool_calls");
      tcs != delta->end() && tcs->is_array()) {
    for (const json& tc : *tcs) {
      const int index = tc.value("index", 0);
      ToolCall& acc = tool_by_index_[index];
      if (auto id = tc.find("id"); id != tc.end() && id->is_string()) {
        acc.id = id->get<std::string>();
      }
      if (auto fn = tc.find("function"); fn != tc.end() && fn->is_object()) {
        if (auto name = fn->find("name");
            name != fn->end() && name->is_string()) {
          acc.name = name->get<std::string>();
        }
        if (auto args = fn->find("arguments");
            args != fn->end() && args->is_string()) {
          acc.arguments += args->get<std::string>();
        }
      }
    }
  }
}

std::vector<ToolCall> StreamParser::tool_calls() const {
  std::vector<ToolCall> out;
  out.reserve(tool_by_index_.size());
  for (const auto& [index, call] : tool_by_index_) {  // map keeps index order
    out.push_back(call);
  }
  return out;
}

}  // namespace llmcli
