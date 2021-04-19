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
  init_pair(Color::BAR, COLOR_BLACK, COLOR_GREEN);
  init_pair(Color::PAUSE_BAR, COLOR_BLACK, COLOR_BLUE);
  init_pair(Color::PAUSE_OVER_BAR, COLOR_BLACK, COLOR_RED);
}

class Pomodoro {
public:
  Pomodoro(WINDOW *window) : win(window) {}

  // Start the next work or break unit. If work or break is already running, do
  // nothing.
  void Start() {
    switch (state) {
    case WORKING:
    case PAUSE:
      // Timer is already running. Do nothing.
      return;
    case WORK_DONE:
      state = PAUSE;
      running = true;
      if (pomodoros_done >= 4) {
        pomodoros_done = 0;
        // Time for a long break, YAY! 15 minutes.
        target_time = 15 * 60;
      } else {
        target_time = 5 * 60; // 5 minutes.
      }
      elapsed_time = 0.0;
      last_update = Clock::now();
      break;
    case PAUSE_DONE:
      state = WORKING;
      running = true;
      target_time = 25 * 60; // 25 minutes.
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
        pomodoros_done += 1;
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
    short bar_color;
    switch (state) {
    case WORKING:
      snprintf(buffer, kBufSize, "work %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = Color::BAR;
      break;
    case WORK_DONE:
      snprintf(buffer, kBufSize, "work DONE");
      bar_color = Color::PAUSE_BAR;
      break;
    case PAUSE:
      snprintf(buffer, kBufSize, "pause %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = Color::PAUSE_BAR;
      break;
    case PAUSE_DONE:
      snprintf(buffer, kBufSize, "pause OVER");
      bar_color = Color::PAUSE_OVER_BAR;
      break;
    }

    int rows, cols;
    getmaxyx(win, rows, cols);
    const attr_t attr = 0;
    mvwinsnstr(win, 0, 1, buffer, kBufSize);
    mvwprintw(win, 0, cols - 2, "%1d", pomodoros_done);
    mvwchgat(win, 0, 0, bar_length, attr, bar_color, nullptr);
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

  WINDOW *win;
  State state = PAUSE_DONE;
  bool running = false;
  float target_time = 0.0;
  float elapsed_time = 0.0;
  TimePoint last_update;
  int pomodoros_done = 0;
};

class Todo {
public:
  Todo(WINDOW *window) : win(window) {
    items.push_back({.text = "foo"});
    items.push_back({.text = "bar", .done = false});
    items.push_back({.text = "baz", .done = true});
  }

  void Up() { current_item = std::max(current_item - 1, 0); }

  void Down() {
    current_item = std::min<int>(current_item + 1, items.size() - 1);
    current_item = std::max(current_item, 0);
  }

  void Toggle() { items[current_item].done = !items[current_item].done; }

  void Draw() const {
    for (int i = 0; i < items.size(); ++i) {
      const Item &item = items[i];
      const char status_char = item.done ? 'x' : ' ';

      attr_t attrs = 0;
      if (i == current_item) {
        attrs |= WA_BOLD;
      }
      if (item.done) {
        attrs |= WA_DIM;
      }

      wattron(win, attrs);
      mvwprintw(win, i, 0, "[%c] %s", status_char, item.text.c_str());
      wattroff(win, attrs);
    }

    // Move to the current item.
    wmove(win, current_item, 1);
  }

  void New() {
    items.insert(items.begin(), {.text = ""});
    current_item = 0;
    werase(win);
    Draw();
    wrefresh(win);

    constexpr int kBufferLength = 32;
    char buffer[kBufferLength];
    nodelay(win, false);
    echo();
    mvwgetnstr(win, /*y=*/current_item, /*x=*/4, buffer, kBufferLength);
    nodelay(win, true);
    noecho();
    items[current_item].text = buffer;
  }

  void Delete() {
    if (items.empty()) {
      return;
    }
    items.erase(items.begin() + current_item);
  }

private:
  struct Item {
    bool done;
    std::string text;
  };

  WINDOW *win;
  int current_item = 0;
  std::vector<Item> items;
};

int main() {
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);

  init_colors();

  WINDOW *pomodoro_window =
      newwin(/*nlines=*/1, /*ncols=*/0, /*begin_y=*/0, /*begin_x=*/0);
  WINDOW *todo_window =
      newwin(/*nlines=*/0, /*ncols=*/0, /*begin_y=*/2, /*begin_x=*/1);

  Pomodoro pomodoro(pomodoro_window);
  Todo todo(todo_window);
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
      pomodoro.Start();
    } else if (ch == 'j') {
      todo.Down();
    } else if (ch == 'k') {
      todo.Up();
    } else if (ch == 'n') {
      todo.New();
    } else if (ch == 'D') {
      todo.Delete();
    } else if (ch == ' ') {
      todo.Toggle();
    }

    pomodoro.Tick();

    werase(pomodoro_window);
    werase(todo_window);
    pomodoro.Draw();
    todo.Draw();
    // refresh();
    wrefresh(pomodoro_window);
    wrefresh(todo_window);
  }

  delwin(pomodoro_window);
  delwin(todo_window);
  endwin();
}
