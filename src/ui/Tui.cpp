#include "ui/Tui.hpp"

#include <clocale>

#include <ncursesw/ncurses.h>

#include "ui/Theme.hpp"

namespace llmcli {

Tui::Tui() {
  std::setlocale(LC_ALL, "");  // enable UTF-8 output
  initscr();
  cbreak();              // line buffering off, signals still delivered
  noecho();              // we echo input ourselves
  keypad(stdscr, TRUE);  // translate arrows/backspace into KEY_* codes
  nonl();                // distinguish Enter from newline
  curs_set(1);
  // Report mouse-wheel events (button 4 = up, button 5 = down) for scrollback.
  mousemask(BUTTON4_PRESSED | BUTTON5_PRESSED, nullptr);
  if (has_colors()) {
    start_color();
    use_default_colors();
    theme::init();
  }
}

Tui::~Tui() { endwin(); }

int Tui::rows() const { return LINES; }
int Tui::cols() const { return COLS; }

}  // namespace llmcli
