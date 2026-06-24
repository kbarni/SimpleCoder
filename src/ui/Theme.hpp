#pragma once

#include <ncursesw/ncurses.h>

namespace llmcli::theme {

// Named color-pair ids. Values are the pair numbers passed to init_pair /
// COLOR_PAIR; 0 is reserved by ncurses for the default pair.
enum Pair : short {
  Speaker = 1,
  Error = 2,
  Border = 3,
  Accent = 4,
  Status = 5,
};

// Define the color pairs above. Safe to call unconditionally: if the terminal
// has no color support this is a no-op and the *_attr() helpers below fall back
// to monochrome attributes. Call once, after start_color().
void init();

// Whether color pairs are usable (terminal supports color and init() ran).
bool has_color();

// Attributes for the chat styles, each with a monochrome fallback.
attr_t accent_attr();   // banner / headers
attr_t error_attr();    // error notices
attr_t dim_attr();      // reasoning, tool activity, separators
attr_t border_attr();   // window frames
attr_t status_attr();   // top status header

}  // namespace llmcli::theme
