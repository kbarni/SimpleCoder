#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#include "app/Attachments.hpp"
#include "util/Base64.hpp"

using namespace llmcli;

namespace {
// A reader backed by an in-memory file table.
FileReader table_reader(std::map<std::string, std::string> files) {
  return [files = std::move(files)](
             const std::string& p) -> std::optional<std::string> {
    auto it = files.find(p);
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
}
}  // namespace

TEST_CASE("a line with no attachments is unchanged", "[attach]") {
  auto r = expand_attachments("just a message", table_reader({}));
  CHECK(r.errors.empty());
  CHECK(r.attached.empty());
  CHECK(r.text == "just a message");
}

TEST_CASE("a single @file is inlined as a fenced block", "[attach]") {
  auto r = expand_attachments("look at @foo.txt please",
                              table_reader({{"foo.txt", "hello\n"}}));
  CHECK(r.errors.empty());
  REQUIRE(r.attached == std::vector<std::string>{"foo.txt"});
  CHECK(r.text ==
        "look at @foo.txt please\n\nAttached file `foo.txt`:\n```\nhello\n```");
}

TEST_CASE("missing newline at EOF is added before the closing fence",
          "[attach]") {
  auto r = expand_attachments("@a", table_reader({{"a", "no-newline"}}));
  CHECK(r.text == "@a\n\nAttached file `a`:\n```\nno-newline\n```");
}

TEST_CASE("multiple attachments are inlined in order", "[attach]") {
  auto r = expand_attachments(
      "@a and @b", table_reader({{"a", "AAA\n"}, {"b", "BBB\n"}}));
  CHECK(r.errors.empty());
  REQUIRE(r.attached == std::vector<std::string>{"a", "b"});
  CHECK(r.text.find("`a`") != std::string::npos);
  CHECK(r.text.find("`b`") != std::string::npos);
  CHECK(r.text.find("AAA") < r.text.find("BBB"));
}

TEST_CASE("a missing file is reported and nothing is attached", "[attach]") {
  auto r = expand_attachments("see @nope.txt", table_reader({}));
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].find("nope.txt") != std::string::npos);
  CHECK(r.attached.empty());
}

TEST_CASE("@ inside a word (email) is not an attachment", "[attach]") {
  auto r = expand_attachments("mail me at user@host.com",
                              table_reader({{"host.com", "x"}}));
  CHECK(r.attached.empty());
  CHECK(r.errors.empty());
  CHECK(r.text == "mail me at user@host.com");
}

TEST_CASE("a bare @ is left untouched", "[attach]") {
  auto r = expand_attachments("what is @ here", table_reader({}));
  CHECK(r.attached.empty());
  CHECK(r.errors.empty());
  CHECK(r.text == "what is @ here");
}

// --- image attachments (T29) -----------------------------------------------

TEST_CASE("image extensions are detected case-insensitively", "[attach][image]") {
  CHECK(is_image_path("photo.png"));
  CHECK(is_image_path("a/b/Shot.JPG"));
  CHECK(is_image_path("x.jpeg"));
  CHECK(is_image_path("x.webp"));
  CHECK_FALSE(is_image_path("notes.txt"));
  CHECK_FALSE(is_image_path("archive.tar.gz"));
  CHECK(image_media_type("x.PNG") == "image/png");
  CHECK(image_media_type("x.jpg") == "image/jpeg");
  CHECK(image_media_type("x.gif") == "image/gif");
}

TEST_CASE("an @image becomes a base64 data-URL part, not inlined text",
          "[attach][image]") {
  const std::string bytes = "\x89PNG\r\n\x1a\n some bytes \xff\xfe\x01";
  auto r = expand_attachments("describe @photo.png please",
                              table_reader({{"photo.png", bytes}}));
  CHECK(r.errors.empty());
  CHECK(r.attached.empty());            // not a text inline
  REQUIRE(r.images.size() == 1);

  const ImagePart& img = r.images[0];
  CHECK(img.name == "photo.png");
  CHECK(img.media_type == "image/png");
  CHECK(img.bytes == bytes.size());
  CHECK(img.data_url.rfind("data:image/png;base64,", 0) == 0);  // prefix
  // The payload decodes back to the original bytes.
  const std::string b64 = img.data_url.substr(img.data_url.find(',') + 1);
  CHECK(base64_decode(b64) == bytes);
  // The @token stays in the text so the model still sees the filename.
  CHECK(r.text == "describe @photo.png please");
}

TEST_CASE("a turn can mix a text file and an image", "[attach][image]") {
  auto r = expand_attachments(
      "@notes.txt and @pic.jpg",
      table_reader({{"notes.txt", "hi\n"}, {"pic.jpg", "JPEGDATA"}}));
  CHECK(r.errors.empty());
  REQUIRE(r.attached == std::vector<std::string>{"notes.txt"});
  REQUIRE(r.images.size() == 1);
  CHECK(r.images[0].media_type == "image/jpeg");
  CHECK(r.text.find("Attached file `notes.txt`") != std::string::npos);
}

// --- image size cap (T34) --------------------------------------------------

TEST_CASE("an image over max_image_bytes is rejected, not attached",
          "[attach][image]") {
  auto r = expand_attachments("@big.png", table_reader({{"big.png", "0123456789"}}),
                              /*max_image_bytes=*/4);
  REQUIRE(r.images.empty());
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].find("too large") != std::string::npos);
  CHECK(r.errors[0].find("big.png") != std::string::npos);
}

TEST_CASE("an image at or under the cap still attaches", "[attach][image]") {
  auto r = expand_attachments("@ok.png", table_reader({{"ok.png", "1234"}}),
                              /*max_image_bytes=*/4);
  CHECK(r.errors.empty());
  REQUIRE(r.images.size() == 1);
}

TEST_CASE("max_image_bytes = 0 disables the cap", "[attach][image]") {
  auto r = expand_attachments("@huge.png",
                              table_reader({{"huge.png", std::string(100000, 'x')}}),
                              /*max_image_bytes=*/0);
  CHECK(r.errors.empty());
  REQUIRE(r.images.size() == 1);
}

TEST_CASE("the size cap only applies to images, not text files",
          "[attach][image]") {
  auto r = expand_attachments("@notes.txt",
                              table_reader({{"notes.txt", std::string(100, 'y')}}),
                              /*max_image_bytes=*/4);
  CHECK(r.errors.empty());
  REQUIRE(r.attached == std::vector<std::string>{"notes.txt"});
}
