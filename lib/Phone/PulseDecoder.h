#pragma once
// Rotary-dial pulse decoder. Pure C++, no Arduino dependencies, so it can be
// unit-tested on the host (see test/test_native).
//
// Usage: call update(nowMs, contactOpen) as often as possible (every loop()
// iteration) with the raw pin state, then drain events with pollEvent().
// Debouncing, pulse counting and the digit/number gap timers all live here.

#include <stdint.h>
#include <stddef.h>

#include "EventQueue.h"

class PulseDecoder {
 public:
  static constexpr size_t kMaxDigits = 24;
  static constexpr size_t kEventQueueSize = 8;

  struct Config {
    uint32_t debounceMs = 15;     // level must be stable this long to count
    uint32_t digitGapMs = 250;    // quiet time that finalizes a digit
    uint32_t numberGapMs = 2000;  // quiet time that finalizes a number;
                                  // 0 disables number grouping entirely
  };

  enum class EventType : uint8_t { Pulse, Digit, Number };

  struct Event {
    EventType type;
    // --- Pulse: emitted on the closing edge of each pulse ---
    uint8_t pulseIndex;   // 1-based index within the current digit
    uint32_t openMs;      // how long the contact was open
    uint32_t closedMs;    // closed time *before* this pulse; 0 for the first
                          // pulse of a digit (no previous pulse to measure from)
    // --- Digit ---
    uint8_t pulses;       // raw pulse count (1..10 is valid)
    int8_t digit;         // 0..9, or -1 if pulses was out of range
    // --- Number ---
    char number[kMaxDigits + 1];  // NUL-terminated; '?' marks invalid digits
    uint8_t length;
  };

  PulseDecoder() = default;
  explicit PulseDecoder(const Config& cfg) : cfg_(cfg) {}

  // Feed the current (raw, un-debounced) contact state. open == true means
  // the contact is open (pin HIGH with pull-up), i.e. mid-pulse.
  void update(uint32_t nowMs, bool open);

  // Pop the next pending event. Returns false when the queue is empty.
  bool pollEvent(Event& out);

  // Debounced contact state as currently seen by the decoder.
  bool contactOpen() const { return stableOpen_; }
  // Pulses counted so far in the digit currently being dialled.
  uint8_t pulsesInProgress() const { return pulsesInDigit_; }
  // Digits accumulated in the number currently being dialled.
  uint8_t digitsInProgress() const { return numberLen_; }

  // Discard any partial digit/number and pending events.
  void reset();

 private:
  void acceptEdge(uint32_t t, bool open);
  void finalizeDigit();
  void finalizeNumber();

  Config cfg_{};

  // Debounce state
  bool started_ = false;
  bool lastRawOpen_ = false;
  bool stableOpen_ = false;
  uint32_t lastRawChangeMs_ = 0;

  // Pulse / digit / number state
  uint32_t lastEdgeMs_ = 0;
  uint32_t openStartMs_ = 0;
  uint32_t lastCloseMs_ = 0;
  bool inPulse_ = false;
  uint8_t pulsesInDigit_ = 0;
  char number_[kMaxDigits + 1] = {0};
  uint8_t numberLen_ = 0;

  EventQueue<Event, kEventQueueSize> events_;
};
