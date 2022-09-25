#ifndef POMODORO_TIME_UTILS_H_
#define POMODORO_TIME_UTILS_H_

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>

#include "state.pb.h"

class Timer {
private:
  // Useful for testing.
  static constexpr double kTimeAcceleration = 100;

public:
  void Start() { start_ = Clock::now(); }
  double ElapsedSeconds() const {
    if (!start_)
      return 0;
    const std::chrono::duration<double> duration = Clock::now() - *start_;
    return duration.count() * kTimeAcceleration;
  }
  void Reset() { start_ = std::nullopt; }

private:
  // Special clock that always runs forward.
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  std::optional<TimePoint> start_;
};

class PomodoroTimer {
public:
  void Start(double target_duration) {
    target_duration_seconds_ = target_duration;
    has_rung_ = false;
    start_ = Clock::now();
    timer_.Start();
  }

  Done Stop() {
    Done done;
    if (!start_)
      return done;
    done.set_start_time(FormatTime(*start_));
    done.set_end_time(FormatTime(Clock::now()));
    done.set_duration_seconds(timer_.ElapsedSeconds());

    start_ = std::nullopt;
    timer_.Reset();
    return done;
  }

  bool active() const { return !!start_; }

  double ElapsedSeconds() const { return timer_.ElapsedSeconds(); }
  double ElapsedFraction() const {
    return timer_.ElapsedSeconds() / target_duration_seconds_;
  }
  double RemainingSeconds() const {
    return std::max(0.0, target_duration_seconds_ - timer_.ElapsedSeconds());
  }
  double OvertimeSeconds() const {
    return std::max(0.0, timer_.ElapsedSeconds() - target_duration_seconds_);
  }

  // Only returns true once, when called after the time is up.
  bool IsRinging() {
    if (has_rung_) {
      return false;
    } else if (ElapsedSeconds() >= target_duration_seconds_) {
      has_rung_ = true;
      return true;
    } else {
      return false;
    }
  }

private:
  using Clock = std::chrono::system_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  Timer timer_;
  double target_duration_seconds_ = 0;
  bool has_rung_ = false;
  std::optional<TimePoint> start_;

  static std::string FormatTime(const TimePoint &time_point) {
    const std::time_t now_c = std::chrono::system_clock::to_time_t(time_point);
    const std::tm now_tm = *std::localtime(&now_c);
    std::stringstream stream;
    stream << std::put_time(&now_tm, "%H:%M");
    return stream.str();
  }
};

#endif // POMODORO_TIME_UTILS_H_
