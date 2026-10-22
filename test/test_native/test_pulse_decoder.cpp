// Host-side unit tests for PulseDecoder. Run with: pio test -e native
#include <unity.h>

#include <string>
#include <vector>

#include "PulseDecoder.h"
#include "PulseStats.h"

using Ev = PulseDecoder::Event;
using Type = PulseDecoder::EventType;

// A simulated contact edge: at time `ms` the raw pin becomes `open`.
struct Edge {
  uint32_t ms;
  bool open;
};

// Replays edge timestamps by stepping the clock 1 ms at a time and feeding
// the level in force at each instant, exactly as loop() would sample it.
// Collects every event the decoder emits. Runs `tailMs` past the last edge.
static std::vector<Ev> play(PulseDecoder& d, const std::vector<Edge>& edges,
                            uint32_t tailMs = 2500, uint32_t startMs = 0) {
  std::vector<Ev> out;
  bool level = false;  // closed at rest
  uint32_t end = (edges.empty() ? startMs : edges.back().ms) + tailMs;
  size_t i = 0;
  for (uint32_t t = startMs; t <= end; ++t) {
    while (i < edges.size() && edges[i].ms == t) level = edges[i++].open;
    d.update(t, level);
    Ev e;
    while (d.pollEvent(e)) out.push_back(e);
  }
  return out;
}

// Build edges for a digit of `n` pulses starting at `t` (open 60 / closed 40).
static void dial(std::vector<Edge>& edges, uint32_t& t, int n,
                 uint32_t openMs = 60, uint32_t closedMs = 40) {
  for (int i = 0; i < n; ++i) {
    edges.push_back({t, true});
    t += openMs;
    edges.push_back({t, false});
    t += closedMs;
  }
}

static std::string digitsOf(const std::vector<Ev>& evs) {
  std::string s;
  for (auto& e : evs)
    if (e.type == Type::Digit) s += (e.digit >= 0 ? char('0' + e.digit) : '?');
  return s;
}

static std::string numberOf(const std::vector<Ev>& evs) {
  for (auto& e : evs)
    if (e.type == Type::Number) return e.number;
  return "<none>";
}

static int countOf(const std::vector<Ev>& evs, Type t) {
  int n = 0;
  for (auto& e : evs) n += (e.type == t);
  return n;
}

// ---------------------------------------------------------------------------

void test_single_digit_5() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 5);
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL(5, countOf(evs, Type::Pulse));
  TEST_ASSERT_EQUAL_STRING("5", digitsOf(evs).c_str());
  TEST_ASSERT_EQUAL_STRING("5", numberOf(evs).c_str());
}

void test_ten_pulses_is_zero() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 10);
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL_STRING("0", digitsOf(evs).c_str());
}

void test_multi_digit_number() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  const int digits[] = {10, 3, 10, 1, 2, 3, 4};  // "0301234"
  for (int n : digits) {
    dial(edges, t, n);
    t += 400;  // inter-digit gap (> 250 ms, < 2000 ms)
  }
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL_STRING("0301234", digitsOf(evs).c_str());
  TEST_ASSERT_EQUAL(1, countOf(evs, Type::Number));
  TEST_ASSERT_EQUAL_STRING("0301234", numberOf(evs).c_str());
}

void test_number_gap_splits_numbers() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 1);
  t += 2500;  // > 2 s -> first number finalized
  dial(edges, t, 2);
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL(2, countOf(evs, Type::Number));
  std::vector<std::string> nums;
  for (auto& e : evs)
    if (e.type == Type::Number) nums.push_back(e.number);
  TEST_ASSERT_EQUAL_STRING("1", nums[0].c_str());
  TEST_ASSERT_EQUAL_STRING("2", nums[1].c_str());
}

void test_pulse_timing_reported() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 3, 62, 38);
  auto evs = play(d, edges);
  std::vector<Ev> pulses;
  for (auto& e : evs)
    if (e.type == Type::Pulse) pulses.push_back(e);
  TEST_ASSERT_EQUAL(3, pulses.size());
  TEST_ASSERT_EQUAL(1, pulses[0].pulseIndex);
  TEST_ASSERT_EQUAL_UINT32(62, pulses[0].openMs);
  TEST_ASSERT_EQUAL_UINT32(0, pulses[0].closedMs);  // no preceding gap
  TEST_ASSERT_EQUAL(2, pulses[1].pulseIndex);
  TEST_ASSERT_EQUAL_UINT32(62, pulses[1].openMs);
  TEST_ASSERT_EQUAL_UINT32(38, pulses[1].closedMs);
  TEST_ASSERT_EQUAL_UINT32(38, pulses[2].closedMs);
}

void test_bounce_is_filtered() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  // Two pulses, each edge followed by a few ms of chatter (< 15 ms debounce).
  for (int i = 0; i < 2; ++i) {
    edges.push_back({t, true});
    edges.push_back({t + 2, false});
    edges.push_back({t + 4, true});
    edges.push_back({t + 7, false});
    edges.push_back({t + 9, true});
    t += 60;
    edges.push_back({t, false});
    edges.push_back({t + 3, true});
    edges.push_back({t + 5, false});
    t += 40;
  }
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL(2, countOf(evs, Type::Pulse));
  TEST_ASSERT_EQUAL_STRING("2", digitsOf(evs).c_str());
}

void test_glitch_shorter_than_debounce_ignored() {
  PulseDecoder d;
  std::vector<Edge> edges;
  edges.push_back({100, true});
  edges.push_back({108, false});  // 8 ms open blip
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL(0, countOf(evs, Type::Pulse));
  TEST_ASSERT_EQUAL(0, countOf(evs, Type::Digit));
}

void test_more_than_ten_pulses_is_invalid() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 12);
  auto evs = play(d, edges);
  TEST_ASSERT_EQUAL_STRING("?", digitsOf(evs).c_str());
  bool found = false;
  for (auto& e : evs)
    if (e.type == Type::Digit) {
      found = true;
      TEST_ASSERT_EQUAL(12, e.pulses);
      TEST_ASSERT_EQUAL(-1, e.digit);
    }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL_STRING("?", numberOf(evs).c_str());
}

void test_open_at_boot_is_not_a_pulse() {
  PulseDecoder d;
  // Line is open from t=0 (e.g. wire off), closes at 500 ms, then a real digit.
  std::vector<Edge> edges;
  edges.push_back({500, false});
  uint32_t t = 1000;
  dial(edges, t, 3);
  std::vector<Ev> out;
  bool level = true;
  size_t i = 0;
  for (uint32_t ts = 0; ts <= t + 2500; ++ts) {
    while (i < edges.size() && edges[i].ms == ts) level = edges[i++].open;
    d.update(ts, level);
    Ev e;
    while (d.pollEvent(e)) out.push_back(e);
  }
  TEST_ASSERT_EQUAL(3, countOf(out, Type::Pulse));
  TEST_ASSERT_EQUAL_STRING("3", digitsOf(out).c_str());
}

void test_millis_wraparound() {
  PulseDecoder d;
  std::vector<Edge> edges;
  uint32_t t = 0xFFFFFFFFu - 250;  // digit straddles the 32-bit rollover
  dial(edges, t, 7);
  // play() loops on uint32_t and would wrap; drive manually instead.
  std::vector<Ev> out;
  bool level = false;
  size_t i = 0;
  uint32_t ts = 0xFFFFFFFFu - 300;
  for (uint32_t n = 0; n < 4000; ++n, ++ts) {
    while (i < edges.size() && edges[i].ms == ts) level = edges[i++].open;
    d.update(ts, level);
    Ev e;
    while (d.pollEvent(e)) out.push_back(e);
  }
  TEST_ASSERT_EQUAL_STRING("7", digitsOf(out).c_str());
  TEST_ASSERT_EQUAL_STRING("7", numberOf(out).c_str());
}

void test_number_grouping_disabled() {
  PulseDecoder::Config cfg;
  cfg.numberGapMs = 0;
  PulseDecoder d(cfg);
  std::vector<Edge> edges;
  uint32_t t = 100;
  dial(edges, t, 4);
  t += 400;
  dial(edges, t, 2);
  auto evs = play(d, edges, 5000);
  TEST_ASSERT_EQUAL_STRING("42", digitsOf(evs).c_str());
  TEST_ASSERT_EQUAL(0, countOf(evs, Type::Number));
}

void test_pulse_stats() {
  PulseStats s;
  s.add(60, 0);   // first pulse: excluded from period/ratio
  s.add(60, 40);
  s.add(62, 38);
  TEST_ASSERT_EQUAL_UINT32(3, s.pulses());
  TEST_ASSERT_EQUAL_UINT32(2, s.pairs());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, s.avgPeriodMs());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 122.0f / 78.0f, s.openClosedRatio());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.667f, s.avgOpenMs());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_digit_5);
  RUN_TEST(test_ten_pulses_is_zero);
  RUN_TEST(test_multi_digit_number);
  RUN_TEST(test_number_gap_splits_numbers);
  RUN_TEST(test_pulse_timing_reported);
  RUN_TEST(test_bounce_is_filtered);
  RUN_TEST(test_glitch_shorter_than_debounce_ignored);
  RUN_TEST(test_more_than_ten_pulses_is_invalid);
  RUN_TEST(test_open_at_boot_is_not_a_pulse);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_number_grouping_disabled);
  RUN_TEST(test_pulse_stats);
  return UNITY_END();
}
