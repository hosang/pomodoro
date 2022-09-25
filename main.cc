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
#include "time_utils.h"

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

class Todo {
public:
  Todo(State &state) : state_(state) {}

  void Up() { current_item = std::max(current_item - 1, 0); }

  void Down() {
    current_item = std::min<int>(current_item + 1, state_.todos().size() - 1);
    current_item = std::max(current_item, 0);
  }

  std::string CurrentTodoText() const {
    const auto &items = state_.todos();
    if (current_item < items.size()) {
      return items[current_item].text;
    } else {
      return "";
    }
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
  int current_item = 0;
};

class Pomodoro {
public:
  Pomodoro(WINDOW *window, State &state, Todo &todo)
      : win(window), state_(state), todo_(todo) {}

  // Start the next work or break unit. If work or break is already running, do
  // nothing.
  void Start() {
    switch (work_state) {
    case WORKING:
    case PAUSE:
      // Timer is already running. Do nothing.
      break;
    case WORK_DONE:
      FinishWork();
      work_state = PAUSE;
      if (pomodoros_done >= 4) {
        pomodoros_done = 0;
        // Time for a long break, YAY!
        timer_.Start(kLongBreakSeconds);
      } else {
        timer_.Start(kShortBreakSeconds);
      }
      break;
    case PAUSE_DONE:
      FinishPause();
      work_state = WORKING;
      timer_.Start(kWorkPhaseSeconds);
      break;
    }
  }

  void Stop() {
    // "Force" current phase to end, so it's possible to start the next one.
    switch (work_state) {
    case WORKING:
      work_state = WORK_DONE;
      break;
    case PAUSE:
      work_state = PAUSE_DONE;
      break;
    case WORK_DONE:
    case PAUSE_DONE:
      // Nothing to do.
      break;
    }
  }

  void FinishPause() {
    if (work_state != PAUSE_DONE || !timer_.active()) {
      return;
    }
    Done done = timer_.Stop();
    done.set_done_type(Done::BREAK);
    state_.AddDone(done);
  }

  void FinishWork() {
    if (work_state != WORK_DONE) {
      return;
    }
    pomodoros_done += 1;
    Done done = timer_.Stop();
    done.set_done_type(Done::WORK);
    done.set_todo(todo_.CurrentTodoText());
    state_.AddDone(done);
  }

  void Reset() {
    switch (work_state) {
    case PAUSE_DONE:
      // Nothing to reset.
      break;
    case WORK_DONE:
    case WORKING: {
      work_state = PAUSE_DONE;
      break;
    }
    case PAUSE: {
      work_state = WORK_DONE;
      break;
    }
    }
  }

  void Tick() {
    // timer_ automatically keeps track of time, even if the target_duration is
    // up.

    // In case the timer *just* finished.
    if ((work_state == WORKING || work_state == PAUSE) && timer_.IsRinging()) {
      // We're done with the current block.
      beep();
      if (work_state == WORKING) {
        work_state = WORK_DONE;
      } else if (work_state == PAUSE) {
        work_state = PAUSE_DONE;
      }
    }
  }

  void Draw() const {
    int bar_length = timer_.ElapsedFraction() * COLS;
    bar_length = std::min(bar_length, COLS);
    bar_length = std::max(bar_length, 1);
    if (work_state == WORK_DONE || work_state == PAUSE_DONE) {
      bar_length = COLS;
    }

    const int remaining = std::lround(timer_.RemainingSeconds());
    constexpr int kBufSize = 32;
    char buffer[kBufSize];
    short bar_color;
    switch (work_state) {
    case WORKING:
      snprintf(buffer, kBufSize, "work %2d:%02d", remaining / 60,
               remaining % 60);
      bar_color = Color::BAR;
      break;
    case WORK_DONE: {
      const int overtime = std::lround(timer_.OvertimeSeconds());
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

  enum WorkState {
    WORKING,
    WORK_DONE,
    PAUSE,
    PAUSE_DONE,
  };

  WINDOW *win;
  State &state_;
  Todo &todo_;
  PomodoroTimer timer_;
  WorkState work_state = PAUSE_DONE;
  int pomodoros_done = 0;
};

std::string GetDay() {
  char buf[sizeof "2021-04-19"];
  time_t now;
  time(&now);
  strftime(buf, sizeof buf, "%F", localtime(&now));
  return std::string(buf);
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

void SaveTodayTxt(const State &state) {
  std::ofstream os(todo_history_path, std::ios_base::app);
  if (!os.is_open()) {
    std::cout << "Could not write to '" << todo_history_path << "'.\n";
    return;
  }

  os << "\n" << state.day() << "\n";
  for (const Done &done : state.history()) {
    if (done.done_type() != Done::WORK)
      continue;
    const int duration_minutes = std::lround(done.duration_seconds() / 60);
    os << "  " << done.start_time() << " " << done.end_time() << " "
       << duration_minutes << "m " << done.todo() << "\n";
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

void DrawToday(WINDOW *win, const State &state) {
  for (const Done &phase : state.history()) {
    const int64_t duration_minutes = std::lround(phase.duration_seconds() / 60);

    if (phase.done_type() == Done::WORK) {
      wcolor_set(win, Color::WORK_BLOCK, NULL);
    } else if (phase.done_type() == Done::BREAK) {
      wcolor_set(win, Color::PAUSE_BLOCK, NULL);
    }
    wprintw(win, " %d ", duration_minutes);
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

  Todo todo(state);
  Pomodoro pomodoro(pomodoro_window.window, state, todo);
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
    } else if (ch == 'S') {
      pomodoro.Stop();
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
    DrawToday(today_window.window, state);
    pomodoro_window.Refresh();
    today_window.Refresh();
    todo_window.Refresh();
  }

  endwin();

  pomodoro.FinishWork();

  SaveState(state.ToProto());
  SaveTodo(day, state.todos());
  SaveTodayTxt(state);
}
