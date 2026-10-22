#pragma once
// Fixed-size ring buffer of events. Oldest event is dropped on overflow.
#include <stddef.h>

template <typename T, size_t N>
class EventQueue {
 public:
  void push(const T& e) {
    if (count_ == N) {
      head_ = (head_ + 1) % N;
      count_--;
    }
    buf_[(head_ + count_) % N] = e;
    count_++;
  }
  bool pop(T& out) {
    if (count_ == 0) return false;
    out = buf_[head_];
    head_ = (head_ + 1) % N;
    count_--;
    return true;
  }
  void clear() { head_ = count_ = 0; }
  size_t size() const { return count_; }

 private:
  T buf_[N];
  size_t head_ = 0;
  size_t count_ = 0;
};
