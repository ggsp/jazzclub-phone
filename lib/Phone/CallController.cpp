#include "CallController.h"

#include <string.h>

const char* CallController::stateName(State s) {
  switch (s) {
    case State::OnHook: return "on-hook";
    case State::OffHook: return "off-hook";
    case State::Dialing: return "dialing";
    case State::Connected: return "connected";
    case State::Unobtainable: return "unobtainable";
  }
  return "?";
}

void CallController::emit(EventType t, uint16_t id, Reason r) {
  Event e{};
  e.type = t;
  memcpy(e.number, number_, len_ + 1);
  e.id = id;
  e.reason = r;
  events_.push(e);
}

void CallController::setHook(uint32_t nowMs, bool offHook) {
  (void)nowMs;
  if (offHook && state_ == State::OnHook) {
    len_ = 0;
    number_[0] = '\0';
    state_ = State::OffHook;
    emit(EventType::Lifted);
  } else if (!offHook && state_ != State::OnHook) {
    state_ = State::OnHook;
    emit(EventType::HungUp);
    len_ = 0;
    number_[0] = '\0';
  }
}

void CallController::fail(Reason r) {
  state_ = State::Unobtainable;
  emit(EventType::Unobtainable, 0, r);
}

void CallController::onDigit(uint32_t nowMs, int8_t digit) {
  if (state_ != State::OffHook && state_ != State::Dialing) return;

  state_ = State::Dialing;
  lastDigitMs_ = nowMs;

  if (digit < 0 || digit > 9 || len_ >= kMaxDigits) {
    fail(Reason::InvalidDigit);
    return;
  }
  number_[len_++] = (char)('0' + digit);
  number_[len_] = '\0';

  uint16_t id = 0;
  switch (plan_.lookup(number_, &id)) {
    case Dialplan::Result::Match:
      state_ = State::Connected;
      emit(EventType::Connected, id);
      break;
    case Dialplan::Result::NoMatch:
      fail(Reason::NoSuchNumber);
      break;
    case Dialplan::Result::Incomplete:
      emit(EventType::DigitAccepted);
      break;
  }
}

void CallController::tick(uint32_t nowMs) {
  if (state_ == State::Dialing && timeoutMs_ > 0 && (nowMs - lastDigitMs_) >= timeoutMs_) {
    fail(Reason::Timeout);
  }
}
