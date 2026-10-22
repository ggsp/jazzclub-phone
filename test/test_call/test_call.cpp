// Host-side tests for Dialplan, Debouncer and CallController.
// Run with: pio test -e native
#include <unity.h>

#include <string>
#include <vector>

#include "CallController.h"
#include "Debouncer.h"
#include "Dialplan.h"

using CC = CallController;
using Type = CC::EventType;

static const Dialplan::Entry kEntries[] = {{"123", 1}, {"000", 2}, {"9", 3}, {"12", 4}};
static const size_t kN = sizeof(kEntries) / sizeof(kEntries[0]);

static std::vector<CC::Event> drain(CC& c) {
  std::vector<CC::Event> v;
  CC::Event e;
  while (c.pollEvent(e)) v.push_back(e);
  return v;
}

static void dialString(CC& c, uint32_t& t, const char* s) {
  for (; *s; ++s) {
    c.onDigit(t, (int8_t)(*s - '0'));
    t += 500;
  }
}

// ---------------------------------------------------------------- Dialplan

void test_dialplan_exact_and_prefix() {
  Dialplan p(kEntries, kN, 3);
  uint16_t id = 0;
  TEST_ASSERT_EQUAL(Dialplan::Result::Incomplete, p.lookup("1", &id));
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("12", &id));  // "12" is an entry
  TEST_ASSERT_EQUAL(4, id);
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("123", &id));
  TEST_ASSERT_EQUAL(1, id);
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("9", &id));
  TEST_ASSERT_EQUAL(3, id);
}

void test_dialplan_no_match() {
  Dialplan p(kEntries, kN, 3);
  TEST_ASSERT_EQUAL(Dialplan::Result::NoMatch, p.lookup("5", nullptr));    // no entry starts with 5
  TEST_ASSERT_EQUAL(Dialplan::Result::NoMatch, p.lookup("13", nullptr));   // 1 ok, 13 dead end
  TEST_ASSERT_EQUAL(Dialplan::Result::Incomplete, p.lookup("00", nullptr));
  TEST_ASSERT_EQUAL(Dialplan::Result::NoMatch, p.lookup("001", nullptr));
}

void test_dialplan_accept_any_full_length() {
  Dialplan p(kEntries, kN, 3, true);
  uint16_t id = 0;
  TEST_ASSERT_EQUAL(Dialplan::Result::Incomplete, p.lookup("5", &id));
  TEST_ASSERT_EQUAL(Dialplan::Result::Incomplete, p.lookup("55", &id));
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("555", &id));
  TEST_ASSERT_EQUAL(555, id);
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("007", &id));
  TEST_ASSERT_EQUAL(7, id);
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("9", &id));  // listed entries still win early
  TEST_ASSERT_EQUAL(3, id);
}

void test_dialplan_set_entries() {
  Dialplan p(kEntries, 0, 3);  // start empty
  TEST_ASSERT_EQUAL(Dialplan::Result::NoMatch, p.lookup("9", nullptr));
  static const Dialplan::Entry more[] = {{"9", 0}, {"42", 0}};
  p.setEntries(more, 2);
  uint16_t id = 7;
  TEST_ASSERT_EQUAL(Dialplan::Result::Match, p.lookup("9", &id));
  TEST_ASSERT_EQUAL(0, id);
  TEST_ASSERT_EQUAL(Dialplan::Result::Incomplete, p.lookup("4", nullptr));
  TEST_ASSERT_EQUAL(2, p.entryCount());
}

// --------------------------------------------------------------- Debouncer

void test_debouncer() {
  Debouncer d(50);
  TEST_ASSERT_FALSE(d.update(0, false));
  TEST_ASSERT_FALSE(d.state());
  TEST_ASSERT_FALSE(d.update(10, true));   // just changed
  TEST_ASSERT_FALSE(d.update(40, true));   // 30 ms, not yet
  TEST_ASSERT_FALSE(d.update(45, false));  // bounce resets
  TEST_ASSERT_FALSE(d.update(50, true));
  TEST_ASSERT_FALSE(d.update(90, true));
  TEST_ASSERT_TRUE(d.update(100, true));   // 50 ms stable
  TEST_ASSERT_TRUE(d.state());
  TEST_ASSERT_FALSE(d.update(101, true));  // no repeat
}

// ---------------------------------------------------------- CallController

void test_digits_ignored_on_hook() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  uint32_t t = 0;
  dialString(c, t, "123");
  TEST_ASSERT_EQUAL(0, drain(c).size());
  TEST_ASSERT_EQUAL(CC::State::OnHook, c.state());
}

void test_lift_dial_connect_hangup() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  uint32_t t = 0;
  c.setHook(t, true);
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Type::Lifted, ev[0].type);
  TEST_ASSERT_EQUAL(CC::State::OffHook, c.state());

  c.onDigit(t, 1);
  c.onDigit(t, 2);  // "12" is itself an entry -> connects right away
  ev = drain(c);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(Type::DigitAccepted, ev[0].type);
  TEST_ASSERT_EQUAL_STRING("1", ev[0].number);
  TEST_ASSERT_EQUAL(Type::Connected, ev[1].type);
  TEST_ASSERT_EQUAL_STRING("12", ev[1].number);
  TEST_ASSERT_EQUAL(4, ev[1].id);
  TEST_ASSERT_EQUAL(CC::State::Connected, c.state());

  c.onDigit(t, 3);  // digits after connect are ignored
  TEST_ASSERT_EQUAL(0, drain(c).size());

  c.setHook(t, false);
  ev = drain(c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Type::HungUp, ev[0].type);
  TEST_ASSERT_EQUAL(CC::State::OnHook, c.state());
  TEST_ASSERT_EQUAL_STRING("", c.dialed());
}

void test_single_digit_number() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  uint32_t t = 0;
  c.setHook(t, true);
  c.onDigit(t, 9);
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(Type::Connected, ev.back().type);
  TEST_ASSERT_EQUAL(3, ev.back().id);
}

void test_unobtainable_no_such_number() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  uint32_t t = 0;
  c.setHook(t, true);
  drain(c);
  c.onDigit(t, 0);
  c.onDigit(t, 0);
  c.onDigit(t, 5);  // "005": dead end
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(3, ev.size());
  TEST_ASSERT_EQUAL(Type::Unobtainable, ev[2].type);
  TEST_ASSERT_EQUAL(CC::Reason::NoSuchNumber, ev[2].reason);
  TEST_ASSERT_EQUAL_STRING("005", ev[2].number);
  TEST_ASSERT_EQUAL(CC::State::Unobtainable, c.state());
  c.onDigit(t, 9);  // ignored after failure
  TEST_ASSERT_EQUAL(0, drain(c).size());
}

void test_unobtainable_invalid_digit() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  c.setHook(0, true);
  drain(c);
  c.onDigit(0, -1);
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Type::Unobtainable, ev[0].type);
  TEST_ASSERT_EQUAL(CC::Reason::InvalidDigit, ev[0].reason);
}

void test_inter_digit_timeout() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  c.setHook(0, true);
  c.onDigit(100, 1);
  drain(c);
  c.tick(5000);
  TEST_ASSERT_EQUAL(0, drain(c).size());
  TEST_ASSERT_EQUAL(CC::State::Dialing, c.state());
  c.tick(10100);
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Type::Unobtainable, ev[0].type);
  TEST_ASSERT_EQUAL(CC::Reason::Timeout, ev[0].reason);
}

void test_no_timeout_before_first_digit() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  c.setHook(0, true);
  drain(c);
  c.tick(60000);
  TEST_ASSERT_EQUAL(0, drain(c).size());
  TEST_ASSERT_EQUAL(CC::State::OffHook, c.state());
}

void test_hangup_mid_dial_resets() {
  Dialplan p(kEntries, kN, 3);
  CC c(p, 10000);
  uint32_t t = 0;
  c.setHook(t, true);
  c.onDigit(t, 1);
  c.setHook(t, false);
  c.setHook(t, true);
  drain(c);
  c.onDigit(t, 2);  // fresh number "2", not "12"
  auto ev = drain(c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Type::Unobtainable, ev[0].type);  // nothing starts with 2
  TEST_ASSERT_EQUAL_STRING("2", ev[0].number);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dialplan_exact_and_prefix);
  RUN_TEST(test_dialplan_no_match);
  RUN_TEST(test_dialplan_accept_any_full_length);
  RUN_TEST(test_dialplan_set_entries);
  RUN_TEST(test_debouncer);
  RUN_TEST(test_digits_ignored_on_hook);
  RUN_TEST(test_lift_dial_connect_hangup);
  RUN_TEST(test_single_digit_number);
  RUN_TEST(test_unobtainable_no_such_number);
  RUN_TEST(test_unobtainable_invalid_digit);
  RUN_TEST(test_inter_digit_timeout);
  RUN_TEST(test_no_timeout_before_first_digit);
  RUN_TEST(test_hangup_mid_dial_resets);
  return UNITY_END();
}
