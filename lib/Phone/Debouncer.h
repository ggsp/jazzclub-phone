#pragma once
// Simple level debouncer: a new level must hold for `stableMs` before it is
// reported. The first sample is adopted immediately as the initial state.
#include <stdint.h>

class Debouncer {
 public:
  explicit Debouncer(uint32_t stableMs) : stableMs_(stableMs) {}

  // Returns true when the debounced level changed on this call.
  bool update(uint32_t nowMs, bool raw) {
    if (!started_) {
      started_ = true;
      raw_ = stable_ = raw;
      lastChangeMs_ = nowMs;
      return false;
    }
    if (raw != raw_) {
      raw_ = raw;
      lastChangeMs_ = nowMs;
    }
    if (raw != stable_ && (nowMs - lastChangeMs_) >= stableMs_) {
      stable_ = raw;
      return true;
    }
    return false;
  }

  bool state() const { return stable_; }

 private:
  uint32_t stableMs_;
  bool started_ = false;
  bool raw_ = false;
  bool stable_ = false;
  uint32_t lastChangeMs_ = 0;
};
