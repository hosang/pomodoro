#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale.h>
#include <string>
#include <thread>
#include <vector>

#include "ncurses.h"

#include "state.h"
#include "state.pb.h"

// constexpr double kTimeAcceleration = 100;
constexpr double kTimeAcceleration = 1;

constexpr double kWorkPhaseSeconds = 25 * 60;
constexpr double kShortBreakSeconds = 5 * 60;
constexpr double kLongBreakSeconds = 15 * 60;

constexpr char todo_txt_path[] = "/Users/hosang/todo.txt";
constexpr char todo_history_path[] = "/Users/hosang/todo.history.txt";
constexpr char state_path[] = "/Users/hosang/todo.StateProto.bp";

enum Color {
  DEFAULT = 1,
  BAR = 2,
  PAUSE_BAR = 3,
  PAUSE_OVER_BAR = 4,

  WORK_BLOCK = 5,
  PAUSE_BLOCK = 6,
};

void init_colors() {
  start_color();
  init_pair(Color::DEFAULT, COLOR_WHITE, COLOR_BLACK);
  init_pair(Color::BAR, COLOR_BLACK, COLOR_GREEN);
  init_pair(Color::PAUSE_BAR, COLOR_BLACK, COLOR_BLUE);
  init_pair(Color::PAUSE_OVER_BAR, COLOR_BLACK, COLOR_YELLOW);

  init_pair(Color::WORK_BLOCK, COLOR_BLACK, COLOR_GREEN);
  init_pair(Color::PAUSE_BLOCK, COLOR_WHITE, COLOR_BLACK);
}

uint64_t unix_timestamp(double offset_seconds) {
  const std::chrono::seconds offset{std::lround(offset_seconds)};
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(
             now.time_since_epoch() - offset)
      .count();
};

class Pomodoro {
public:
  struct Phase {
    int64_t length_seconds;
    uint64_t start_timestamp;
  };
  std::vector<Phase> phases;

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
      FinishWork();
      state = PAUSE;
      if (pomodoros_done >= 4) {
        pomodoros_done = 0;
        // Time for a long break, YAY!
        target_time = kLongBreakSeconds;
      } else {
        target_time = kShortBreakSeconds;
      }
      elapsed_time = 0.0;
      last_update = Clock::now();
      break;
    case PAUSE_DONE:
      state = WORKING;
      target_time = kWorkPhaseSeconds;
      elapsed_time = 0.0;
      last_update = Clock::now();
      break;
    }
  }

  void FinishWork() {
    if (state != WORK_DONE) {
      return;
    }
    pomodoros_done += 1;
    phases.push_back({.start_timestamp = unix_timestamp(elapsed_time),
                      .length_seconds = std::lround(elapsed_time)});
  }

  void Reset() {
    switch (state) {
    case PAUSE_DONE:
      // Nothing to reset.
      break;
    case WORK_DONE:
    case WORKING: {
      state = PAUSE_DONE;
      elapsed_time = target_time = kShortBreakSeconds;
      break;
    }
    case PAUSE: {
      state = WORK_DONE;
      elapsed_time = target_time = kWorkPhaseSeconds;
      break;
    }
    }
  }

  void Tick() {
    // Keep track of elapsing time.
    if (state == WORKING || state == WORK_DONE || state == PAUSE) {
      // We keep counting time during work done to keep track of "overtime".
      const TimePoint now = Clock::now();
      std::chrono::duration<float> since_tick = now - last_update;
      elapsed_time += since_tick.count() * kTimeAcceleration;
      last_update = now;
    }

    // In case the timer *just* finished.
    if ((state == WORKING || state == PAUSE) && elapsed_time >= target_time) {
      // We're done with the current block.
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
    if (state == WORK_DONE || state == PAUSE_DONE) {
      bar_length = COLS;
    }

    const int remaining = std::lround(target_time - elapsed_time);
    constexpr int kBufSize = 32;
    char buffer[kBufSize];
    short bar_color;
    switch (state) {
    case WORKING:
      snprintf(buffer, kBufSize, "work %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = Color::BAR;
      break;
    case WORK_DONE: {
      const int overtime = std::lround(elapsed_time - target_time);
      snprintf(buffer, kBufSize, "work DONE (+%d:%02d)", overtime / 60,
               overtime % 60);
      bar_color = Color::PAUSE_BAR;
      break;
    }
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
    static_cast<void>(rows);
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
  float target_time = 0.0;
  float elapsed_time = 0.0;
  TimePoint last_update;
  int pomodoros_done = 0;
};

class Todo {
public:
  struct Item {
    bool done;
    std::string text;
  };

  int current_item = 0;

  Todo(State &state) : state_(state) {}

  void Up() { current_item = std::max(current_item - 1, 0); }

  void Down() {
    current_item = std::min<int>(current_item + 1, state_.todos().size() - 1);
    current_item = std::max(current_item, 0);
  }

  void Toggle() { state_.ToggleTodo(current_item); }

  void Draw(WINDOW *win) const {
    const auto &items = state_.todos();
    for (int i = 0; i < items.size(); ++i) {
      const auto &item = items[i];
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

  void New(WINDOW *win) {
    current_item = 0;
    werase(win);
    Draw(win);
    wrefresh(win);

    constexpr int kBufferLength = 64;
    char buffer[kBufferLength];
    nodelay(win, false);
    echo();
    mvwgetnstr(win, /*y=*/current_item, /*x=*/4, buffer, kBufferLength);
    nodelay(win, true);
    noecho();
    state_.AddTodoFront(buffer);
  }

  void Delete() { state_.DeleteTodo(current_item); }

private:
  State &state_;
};

std::string GetDay() {
  char buf[sizeof "2021-04-19"];
  time_t now;
  time(&now);
  strftime(buf, sizeof buf, "%F", localtime(&now));
  return std::string(buf);
}

std::vector<Todo::Item> LoadTodo(std::string &day) {
  std::ifstream is(todo_txt_path);
  if (!is.is_open()) {
    std::cout << "Could not open '" << todo_txt_path << "'.\n";
    return {};
  }

  // TODO: Also store todo items from most recent entry, so items that are not
  // DONE can be copied to today.
  std::string line;
  while (std::getline(is, line)) {
    if (line.length() == 0) {
      continue;
    }

    if (isdigit(line[0])) {
      // Assuming this is a date.
    }
    std::cout << line << std::endl;
  }

  return {};
}

void SaveTodo(const std::string &day, const std::vector<State::Todo> &items) {
  std::ofstream os(todo_txt_path, std::ios_base::app);
  if (!os.is_open()) {
    std::cout << "Could not write to '" << todo_txt_path << "'.\n";
    return;
  }

  os << "\n";
  os << day << "\n";
  for (const State::Todo &item : items) {
    char done_indicator = item.done ? 'x' : ' ';
    os << " " << done_indicator << " " << item.text << "\n";
  }
}

// TODO: Come up with a better name than "phases".
void SavePhases(const std::string &day,
                const std::vector<Pomodoro::Phase> &phases) {
  std::ofstream os(todo_history_path, std::ios_base::app);
  if (!os.is_open()) {
    std::cout << "Could not write to '" << todo_history_path << "'.\n";
    return;
  }

  using seconds = std::chrono::duration<uint64_t>;
  using sys_seconds =
      std::chrono::time_point<std::chrono::system_clock, seconds>;
  for (const auto &phase : phases) {
    const auto timestamp = std::chrono::system_clock::to_time_t(
        sys_seconds(seconds(phase.start_timestamp)));
    os << day << " " << std::put_time(std::localtime(&timestamp), "%FT%T")
       << " " << phase.start_timestamp << " " << phase.length_seconds << "\n";
  }
}

StateProto LoadState() {
  StateProto state;
  std::ifstream is(state_path, std::ios::binary);
  state.ParseFromIstream(&is);
  return state;
}

void SaveState(const StateProto &state_proto) {
  std::ofstream os(state_path, std::ios::binary);
  state_proto.SerializeToOstream(&os);
}

class NCursesWindow {
public:
  WINDOW *window;
  struct Args {
    int nlines;
    int ncols;
    int begin_y;
    int begin_x;
  };

  explicit NCursesWindow(Args args)
      : window(newwin(args.nlines, args.ncols, args.begin_y, args.begin_x)) {}
  ~NCursesWindow() { delwin(window); }

  void Erase() { werase(window); }
  void Refresh() { wrefresh(window); }
};

int64_t TimestampDifference(uint64_t a, uint64_t b) {
  // Avoid overflow.
  if (a > b) {
    return a - b;
  } else {
    return -static_cast<int64_t>(b - a);
  }
}

void DrawToday(WINDOW *win, const std::vector<Pomodoro::Phase> &phases) {
  for (int i = 0; i < phases.size(); ++i) {
    const auto &phase = phases[i];
    const int64_t work_duration_minutes = phase.length_seconds / 60;

    wcolor_set(win, Color::WORK_BLOCK, NULL);
    wprintw(win, " %d ", work_duration_minutes);

    if (i + 1 < phases.size()) {
      const auto &next_phase = phases[i + 1];
      const int64_t break_duration_minutes =
          TimestampDifference(next_phase.start_timestamp,
                              phase.start_timestamp + phase.length_seconds) /
          60;
      wcolor_set(win, Color::PAUSE_BLOCK, NULL);
      wprintw(win, " %d ", break_duration_minutes);
    }
  }
}

int main() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  const std::string day = GetDay();
  State state(LoadState());
  if (state.day() != day) {
    state.ClearHistory();
    state.SetDay(day);
  }

  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);

  init_colors();

  NCursesWindow pomodoro_window(
      {.nlines = 1, .ncols = 0, .begin_y = 0, .begin_x = 0});
  NCursesWindow today_window(
      {.nlines = 1, .ncols = 0, .begin_y = 2, .begin_x = 1});
  NCursesWindow todo_window(
      {.nlines = 0, .ncols = 0, .begin_y = 4, .begin_x = 1});

  Pomodoro pomodoro(pomodoro_window.window);
  Todo todo(state);
  nodelay(stdscr, TRUE);
  for (;;) {
    int ch = getch();
    if (ch == ERR) {
      // No keypress.
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } else if (ch == 'q') {
      // Quit.
      break;
    } else if (ch == 's') {
      pomodoro.Start();
    } else if (ch == 'r') {
      pomodoro.Reset();
    } else if (ch == 'j' || ch == KEY_DOWN) {
      todo.Down();
    } else if (ch == 'k' || ch == KEY_UP) {
      todo.Up();
    } else if (ch == 'n') {
      todo.New(todo_window.window);
    } else if (ch == 'D') {
      todo.Delete();
    } else if (ch == ' ') {
      todo.Toggle();
    }

    pomodoro.Tick();

    pomodoro_window.Erase();
    todo_window.Erase();
    today_window.Erase();
    pomodoro.Draw();
    todo.Draw(todo_window.window);
    DrawToday(today_window.window, pomodoro.phases);
    pomodoro_window.Refresh();
    today_window.Refresh();
    todo_window.Refresh();
  }

  endwin();

  pomodoro.FinishWork();

  SaveState(state.ToProto());
  SaveTodo(day, state.todos());
  SavePhases(day, pomodoro.phases);
}
