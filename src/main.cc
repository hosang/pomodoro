#include <chrono>
#include <cmath>
#include <locale.h>
#include <string>
#include <thread>
#include <vector>

#include "ncurses.h"

enum Color {
  DEFAULT = 1,
  BAR = 2,
  PAUSE_BAR = 3,
  PAUSE_OVER_BAR = 4,
};

void init_colors() {
  start_color();
  init_pair(Color::DEFAULT, COLOR_WHITE, COLOR_BLACK);
  init_pair(Color::BAR, COLOR_WHITE, COLOR_GREEN);
  init_pair(Color::PAUSE_BAR, COLOR_WHITE, COLOR_BLUE);
  init_pair(Color::PAUSE_OVER_BAR, COLOR_WHITE, COLOR_RED);
}

// TODO: Need a way to figure out how long the next pause should be.
class Pomodoro {
public:
  void Start(float seconds) {
    switch (state) {
    case WORKING:
    case PAUSE:
      break;
    case WORK_DONE:
      state = PAUSE;
      running = true;
      target_time = seconds;
      elapsed_time = 0.0;
      last_update = Clock::now();
      break;
    case PAUSE_DONE:
      state = WORKING;
      running = true;
      target_time = seconds;
      elapsed_time = 0.0;
      last_update = Clock::now();
      break;
    }
  }

  void Tick() {
    if (running) {
      const TimePoint now = Clock::now();
      std::chrono::duration<float> since_tick = now - last_update;
      elapsed_time += since_tick.count();
      last_update = now;
    }

    if (running && elapsed_time >= target_time) {
      // We're done with the current block.
      running = false;
      beep();
      if (state == WORKING) {
        state = WORK_DONE;
      } else if (state == PAUSE) {
        state = PAUSE_DONE;
      }
    }
  }

  void Draw() const {
    int bar_length = elapsed_time / target_time * COLS;
    bar_length = std::min(bar_length, COLS);
    bar_length = std::max(bar_length, 1);
    if (!running) {
      bar_length = COLS;
    }

    const int remaining = std::lround(target_time - elapsed_time);
    constexpr int kBufSize = 16;
    char buffer[kBufSize];
    const chtype bg_color = COLOR_PAIR(Color::DEFAULT);
    chtype bar_color;
    switch (state) {
    case WORKING:
      snprintf(buffer, kBufSize, " work %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = COLOR_PAIR(Color::BAR);
      break;
    case WORK_DONE:
      snprintf(buffer, kBufSize, " work DONE");
      bar_color = COLOR_PAIR(Color::PAUSE_BAR);
      break;
    case PAUSE:
      snprintf(buffer, kBufSize, " pause %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = COLOR_PAIR(Color::PAUSE_BAR);
      break;
    case PAUSE_DONE:
      snprintf(buffer, kBufSize, " pause OVER");
      bar_color = COLOR_PAIR(Color::PAUSE_OVER_BAR);
      break;
    }

    const int bufferlen = strlen(buffer);

    for (int x = 0; x < COLS; ++x) {
      const bool in_bar = x < bar_length;
      const chtype color = in_bar ? bar_color : bg_color;
      const chtype c = (x < bufferlen) ? buffer[x] : ' ';
      mvwaddch(stdscr, 0, x, c | color | A_BOLD);
    }
  }

private:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  enum State {
    WORKING,
    WORK_DONE,
    PAUSE,
    PAUSE_DONE,
  };

  State state = PAUSE_DONE;
  bool running = false;
  float target_time = 0.0;
  float elapsed_time = 0.0;
  TimePoint last_update;
};

class Todo {
public:
  Todo() : win(stdscr) {
    items.push_back({.text = "foo"});
    items.push_back({.text = "bar"});
  }

  void Draw() const {
    constexpr int kLineLength = 16;
    char line[kLineLength];

    for (int i = 0; i < items.size(); ++i) {
      const Item &item = items[i];
      char status_char = ' ';
      snprintf(line, kLineLength, " [%c] %s", status_char, item.text.c_str());
      wmove(win, 2 + i, 0);
      waddstr(win, line);
    }

    // Move to the current item.
    wmove(win, 2, 2);
  }

private:
  struct Item {
    bool done;
    bool doing;
    std::string text;
  };

  WINDOW *win;
  std::vector<Item> items;
};

int main() {
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);

  init_colors();

  Pomodoro pomodoro;
  Todo todo;
  nodelay(stdscr, TRUE);
  for (;;) {
    int ch = getch();
    if (ch == ERR) {
      // No keypress.
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
    } else if (ch == 'q') {
      // Quit.
      break;
    } else if (ch == 's') {
      pomodoro.Start(10);
    }

    pomodoro.Tick();
    pomodoro.Draw();
    todo.Draw();
  }

  endwin();
}
