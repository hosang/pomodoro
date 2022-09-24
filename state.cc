#include "state.h"

#include "state.pb.h"

State::State(const StateProto &proto) : day_(proto.history().day()) {
  for (const std::string &todo_descr : proto.todo()) {
    AddTodo(todo_descr);
  }
  if (todos_.empty()) {
    AddTodo("Make TODO list");
  }
}

StateProto State::ToProto() const {
  StateProto proto;
  proto.mutable_history()->set_day(day_);

  for (const Todo &todo : todos_) {
    if (!todo.done) {
      proto.add_todo(todo.text);
    }
  }

  return proto;
}
