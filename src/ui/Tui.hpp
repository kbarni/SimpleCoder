#pragma once

namespace llmcli {

// RAII guard for the ncurses screen. Constructing it initialises curses
// (raw-ish input, no echo, keypad, UTF-8 locale); destruction restores the
// terminal via endwin(). Non-copyable so the terminal state has a single owner.
class Tui {
 public:
  Tui();
  ~Tui();

  Tui(const Tui&) = delete;
  Tui& operator=(const Tui&) = delete;

  // Screen dimensions of the standard screen.
  int rows() const;
  int cols() const;
};

}  // namespace llmcli
