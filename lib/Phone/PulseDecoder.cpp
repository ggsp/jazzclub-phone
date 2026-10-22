#include "PulseDecoder.h"

#include <string.h>

void PulseDecoder::reset() {
  pulsesInDigit_ = 0;
  numberLen_ = 0;
  number_[0] = '\0';
  inPulse_ = false;
  events_.clear();
}

void PulseDecoder::update(uint32_t nowMs, bool open) {
  if (!started_) {
    // Adopt whatever the line looks like at power-up as the resting state
    // instead of treating a miswired/open line as a giant first pulse.
    started_ = true;
    lastRawOpen_ = open;
    stableOpen_ = open;
    lastRawChangeMs_ = nowMs;
    lastEdgeMs_ = nowMs;
    return;
  }

  // --- Debounce: a new level must hold for debounceMs before it counts. ---
  if (open != lastRawOpen_) {
    lastRawOpen_ = open;
    lastRawChangeMs_ = nowMs;
  }
  if (open != stableOpen_ && (nowMs - lastRawChangeMs_) >= cfg_.debounceMs) {
    stableOpen_ = open;
    // The edge really happened when the level last changed, not when the
    // debounce window expired, so timestamp it there for accurate timing.
    acceptEdge(lastRawChangeMs_, open);
  }

  // --- Gap timers. Only run while the contact is at rest (closed). ---
  if (stableOpen_) return;
  uint32_t quiet = nowMs - lastEdgeMs_;
  if (pulsesInDigit_ > 0 && quiet >= cfg_.digitGapMs) {
    finalizeDigit();
  }
  if (cfg_.numberGapMs > 0 && numberLen_ > 0 && pulsesInDigit_ == 0 &&
      quiet >= cfg_.numberGapMs) {
    finalizeNumber();
  }
}

void PulseDecoder::acceptEdge(uint32_t t, bool open) {
  if (open) {
    openStartMs_ = t;
    inPulse_ = true;
  } else if (!inPulse_) {
    // Closing edge without a preceding opening edge (line was open at
    // power-up, e.g. wire off): not a pulse.
  } else {
    // Closing edge: one complete pulse.
    inPulse_ = false;
    Event e{};
    e.type = EventType::Pulse;
    e.openMs = t - openStartMs_;
    e.closedMs = (pulsesInDigit_ > 0) ? (openStartMs_ - lastCloseMs_) : 0;
    if (pulsesInDigit_ < 255) pulsesInDigit_++;
    e.pulseIndex = pulsesInDigit_;
    events_.push(e);
    lastCloseMs_ = t;
  }
  lastEdgeMs_ = t;
}

void PulseDecoder::finalizeDigit() {
  Event e{};
  e.type = EventType::Digit;
  e.pulses = pulsesInDigit_;
  e.digit = (pulsesInDigit_ >= 1 && pulsesInDigit_ <= 10)
                ? static_cast<int8_t>(pulsesInDigit_ % 10)
                : -1;
  pulsesInDigit_ = 0;
  events_.push(e);

  if (cfg_.numberGapMs == 0) return;  // number grouping disabled
  if (numberLen_ < kMaxDigits) {
    number_[numberLen_++] = (e.digit >= 0) ? static_cast<char>('0' + e.digit) : '?';
    number_[numberLen_] = '\0';
  }
  if (numberLen_ >= kMaxDigits) {
    finalizeNumber();  // buffer full; flush rather than drop digits
  }
}

void PulseDecoder::finalizeNumber() {
  Event e{};
  e.type = EventType::Number;
  e.length = numberLen_;
  memcpy(e.number, number_, numberLen_ + 1);
  events_.push(e);
  numberLen_ = 0;
  number_[0] = '\0';
}

bool PulseDecoder::pollEvent(Event& out) { return events_.pop(out); }
