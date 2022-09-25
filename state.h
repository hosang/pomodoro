#ifndef POMODORO_STATE_H_
#define POMODORO_STATE_H_

#include "state.pb.h"

class State {
public:
  struct Todo {
    bool done;
    std::string text;
  };

  State(const StateProto &proto);
  StateProto ToProto() const;

  const std::string &day() const { return day_; }
  const std::vector<Todo> &todos() const { return todos_; }
  const std::vector<Done> &history() const { return history_; }

  // Manipulate todo list.
  void AddTodo(const std::string &text) {
    todos_.push_back({.done = false, .text = text});
  }
  void AddTodoFront(const std::string &text) {
    todos_.insert(todos_.begin(), {.done = false, .text = text});
  }
  void ToggleTodo(int index) {
    if (index >= todos_.size()) {
      return;
    }
    todos_[index].done = !todos_[index].done;
  }
  void DeleteTodo(int index) {
    if (index >= todos_.size()) {
      return;
    }
    todos_.erase(todos_.begin() + index);
  }

  // Manipulate history.
  void SetDay(const std::string &day) { day_ = day; }
  void ClearHistory() { history_.clear(); }
  void AddDone(const Done &done) { history_.push_back(done); }

private:
  std::string day_;
  std::vector<Todo> todos_;
  std::vector<Done> history_;
};

#endif // POMODORO_STATE_H_
