#include "ui/Theme.hpp"

namespace llmcli::theme {

namespace {
bool g_color = false;
}

void init() {
  if (!has_colors()) {
    g_color = false;
    return;
  }
  // start_color() / use_default_colors() are called by Tui before this.
  // -1 = the terminal's default foreground/background (preserves transparency).
  init_pair(Speaker, COLOR_CYAN, -1);
  init_pair(Error, COLOR_RED, -1);
  init_pair(Border, COLOR_BLUE, -1);
  init_pair(Accent, COLOR_GREEN, -1);
  init_pair(Status, COLOR_BLACK, COLOR_CYAN);
  g_color = true;
}

bool has_color() { return g_color; }

attr_t accent_attr() {
  return g_color ? (COLOR_PAIR(Accent) | A_BOLD) : A_BOLD;
}

attr_t error_attr() {
  return g_color ? (COLOR_PAIR(Error) | A_BOLD) : A_BOLD;
}

attr_t dim_attr() { return A_DIM; }

attr_t border_attr() { return g_color ? COLOR_PAIR(Border) : A_NORMAL; }

attr_t status_attr() {
  return g_color ? COLOR_PAIR(Status) : A_REVERSE;
}

}  // namespace llmcli::theme
