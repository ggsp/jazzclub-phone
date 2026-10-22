#pragma once
// Accumulates pulse timing for the self-test summary. Arduino-free.

#include <stdint.h>

class PulseStats {
 public:
  // closedMs == 0 means "first pulse of a digit": the open time is still
  // meaningful, but there is no preceding closed gap, so it is excluded from
  // period and ratio calculations.
  void add(uint32_t openMs, uint32_t closedMs) {
    openCount_++;
    openSum_ += openMs;
    if (closedMs > 0) {
      pairCount_++;
      pairOpenSum_ += openMs;
      closedSum_ += closedMs;
    }
  }

  void clear() { *this = PulseStats{}; }

  uint32_t pulses() const { return openCount_; }
  uint32_t pairs() const { return pairCount_; }

  float avgOpenMs() const { return openCount_ ? (float)openSum_ / openCount_ : 0.f; }
  float avgClosedMs() const { return pairCount_ ? (float)closedSum_ / pairCount_ : 0.f; }
  // Mean open+closed over pulses that have a preceding closed gap.
  float avgPeriodMs() const {
    return pairCount_ ? (float)(pairOpenSum_ + closedSum_) / pairCount_ : 0.f;
  }
  // Total open time / total closed time (nominal 60/40 = 1.5).
  float openClosedRatio() const {
    return closedSum_ ? (float)pairOpenSum_ / (float)closedSum_ : 0.f;
  }

 private:
  uint32_t openCount_ = 0;
  uint32_t openSum_ = 0;
  uint32_t pairCount_ = 0;
  uint32_t pairOpenSum_ = 0;
  uint32_t closedSum_ = 0;
};
