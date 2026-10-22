#pragma once
// Call state machine: hook + digits -> connected / unobtainable.
// Pure C++, no Arduino dependencies.
//
//   OnHook --lift--> OffHook --digit--> Dialing --match--> Connected
//                                         |--no match / timeout / bad digit--> Unobtainable
//   any state --hang up--> OnHook
//
// Digits are ignored while on hook, and after the call has connected or
// failed. Hanging up always resets everything.

#include <stdint.h>

#include "Dialplan.h"
#include "EventQueue.h"

class CallController {
 public:
  static constexpr size_t kMaxDigits = 16;

  enum class State : uint8_t { OnHook, OffHook, Dialing, Connected, Unobtainable };
  enum class EventType : uint8_t { Lifted, HungUp, DigitAccepted, Connected, Unobtainable };
  enum class Reason : uint8_t { None, NoSuchNumber, InvalidDigit, Timeout };

  struct Event {
    EventType type;
    char number[kMaxDigits + 1];  // digits dialled so far
    uint16_t id;                  // destination id (Connected only)
    Reason reason;                // Unobtainable only
  };

  CallController(const Dialplan& plan, uint32_t interDigitTimeoutMs)
      : plan_(plan), timeoutMs_(interDigitTimeoutMs) {}

  void setHook(uint32_t nowMs, bool offHook);
  void onDigit(uint32_t nowMs, int8_t digit);  // -1 = invalid digit
  void tick(uint32_t nowMs);
  bool pollEvent(Event& out) { return events_.pop(out); }

  State state() const { return state_; }
  bool offHook() const { return state_ != State::OnHook; }
  const char* dialed() const { return number_; }

  static const char* stateName(State s);

 private:
  void fail(Reason r);
  void emit(EventType t, uint16_t id = 0, Reason r = Reason::None);

  const Dialplan& plan_;
  uint32_t timeoutMs_;
  State state_ = State::OnHook;
  char number_[kMaxDigits + 1] = {0};
  uint8_t len_ = 0;
  uint32_t lastDigitMs_ = 0;
  EventQueue<Event, 8> events_;
};
