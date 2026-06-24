#include "net/Html.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace llmcli {

namespace {

std::string to_lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

bool ci_starts_with(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

// Case-insensitive search for `needle` in `hay` starting at `from`.
std::size_t find_ci(std::string_view hay, std::string_view needle,
                    std::size_t from) {
  if (needle.empty() || hay.size() < needle.size()) return std::string_view::npos;
  for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
    if (ci_starts_with(hay.substr(i), needle)) return i;
  }
  return std::string_view::npos;
}

// Block-level tags whose boundary should become a newline; everything else
// becomes a space.
bool is_block_tag(const std::string& tag) {
  static const std::unordered_map<std::string, bool> block = {
      {"p", true},     {"div", true},        {"br", true},
      {"li", true},    {"ul", true},         {"ol", true},
      {"tr", true},    {"table", true},      {"h1", true},
      {"h2", true},    {"h3", true},         {"h4", true},
      {"h5", true},    {"h6", true},         {"section", true},
      {"article", true}, {"header", true},   {"footer", true},
      {"nav", true},   {"blockquote", true}, {"pre", true},
      {"hr", true},    {"body", true},       {"html", true},
      {"head", true}};
  return block.count(tag) != 0;
}

void append_entity(std::string& out, std::string_view name) {
  static const std::unordered_map<std::string, std::string> named = {
      {"amp", "&"},   {"lt", "<"},    {"gt", ">"},   {"quot", "\""},
      {"apos", "'"},  {"nbsp", " "},  {"#39", "'"},  {"mdash", "—"},
      {"ndash", "–"}, {"hellip", "…"},{"copy", "©"}, {"reg", "®"}};
  if (auto it = named.find(std::string(name)); it != named.end()) {
    out += it->second;
    return;
  }
  // Numeric: &#NNN; or &#xHH;
  if (!name.empty() && name.front() == '#') {
    std::int32_t cp = -1;
    if (name.size() > 2 && (name[1] == 'x' || name[1] == 'X')) {
      cp = 0;
      for (std::size_t i = 2; i < name.size(); ++i) {
        const char c = name[i];
        cp = cp * 16 + (std::isdigit(static_cast<unsigned char>(c))
                            ? c - '0'
                            : std::tolower(static_cast<unsigned char>(c)) -
                                  'a' + 10);
      }
    } else if (name.size() > 1) {
      cp = 0;
      for (std::size_t i = 1; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) { cp = -1; break; }
        cp = cp * 10 + (name[i] - '0');
      }
    }
    if (cp >= 0 && cp <= 0x10FFFF) {
      // Encode the code point as UTF-8.
      if (cp < 0x80) {
        out += static_cast<char>(cp);
      } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      }
      return;
    }
  }
  // Unknown entity: emit it verbatim so nothing is silently lost.
  out += '&';
  out += std::string(name);
  out += ';';
}

std::string decode_entities(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '&') {
      const std::size_t semi = in.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 10) {
        append_entity(out, std::string_view(in).substr(i + 1, semi - i - 1));
        i = semi;
        continue;
      }
    }
    out += in[i];
  }
  return out;
}

// Collapse runs of spaces/tabs to one space, trim each line, and squeeze three
// or more blank lines down to one. Leading/trailing whitespace is removed.
std::string collapse_ws(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  bool pending_space = false;
  int pending_newlines = 0;
  bool seen_content = false;
  for (const char c : in) {
    if (c == ' ' || c == '\t' || c == '\r') {
      pending_space = true;
    } else if (c == '\n') {
      pending_space = false;
      ++pending_newlines;
    } else {
      if (seen_content) {
        if (pending_newlines > 0) {
          out.append(std::min(pending_newlines, 2), '\n');
        } else if (pending_space) {
          out += ' ';
        }
      }
      pending_newlines = 0;
      pending_space = false;
      out += c;
      seen_content = true;
    }
  }
  return out;
}

bool looks_like_html(std::string_view body) {
  const std::string_view head =
      body.substr(0, std::min<std::size_t>(body.size(), 1024));
  return find_ci(head, "<!doctype", 0) != std::string_view::npos ||
         find_ci(head, "<html", 0) != std::string_view::npos ||
         find_ci(head, "<body", 0) != std::string_view::npos ||
         find_ci(head, "<div", 0) != std::string_view::npos ||
         find_ci(head, "<p>", 0) != std::string_view::npos;
}

}  // namespace

std::string html_to_text(std::string_view body, std::string_view content_type) {
  const std::string ct = to_lower(content_type);
  bool html = ct.find("html") != std::string::npos;
  if (!html) {
    if (ct.find("json") != std::string::npos ||
        ct.find("text/plain") != std::string::npos ||
        ct.find("xml") != std::string::npos) {
      return std::string(body);
    }
    if (ct.empty()) html = looks_like_html(body);
  }
  if (!html) return std::string(body);

  std::string out;
  out.reserve(body.size());
  const std::size_t n = body.size();
  std::size_t i = 0;
  while (i < n) {
    if (body[i] != '<') {
      // In-text whitespace (incl. source newlines) is soft and collapses to a
      // space; only block-level tags below emit a hard '\n'.
      const char c = body[i++];
      out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
      continue;
    }
    const std::string_view rest = body.substr(i);
    // Drop <script>…</script> and <style>…</style> wholesale.
    if (ci_starts_with(rest, "<script") || ci_starts_with(rest, "<style")) {
      const std::string_view close =
          ci_starts_with(rest, "<script") ? "</script" : "</style";
      const std::size_t end = find_ci(body, close, i + 1);
      i = (end == std::string_view::npos) ? n : end;
      while (i < n && body[i] != '>') ++i;  // skip past the closing tag
      if (i < n) ++i;
      out += '\n';
      continue;
    }
    // Generic tag: read its name to decide block vs. inline, then skip to '>'.
    std::size_t j = i + 1;
    if (j < n && body[j] == '/') ++j;
    const std::size_t name_start = j;
    while (j < n && std::isalnum(static_cast<unsigned char>(body[j]))) ++j;
    const std::string tag = to_lower(body.substr(name_start, j - name_start));
    std::size_t k = i;
    while (k < n && body[k] != '>') ++k;
    i = (k < n) ? k + 1 : n;
    out += is_block_tag(tag) ? '\n' : ' ';
  }

  return collapse_ws(decode_entities(out));
}

}  // namespace llmcli
