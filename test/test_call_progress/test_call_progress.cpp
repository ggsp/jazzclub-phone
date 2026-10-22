// Host-side tests for the call-progress sequence builder.
// Run with: pio test -e native
#include <unity.h>

#include "CallProgress.h"

using namespace callprogress;

static Timing timing() {
  Timing t{};
  t.leadMs = 300;
  t.dtmfOnMs = 120;
  t.dtmfOffMs = 80;
  t.ringDelayMs = 800;
  t.ringToneHz = 425;
  t.ringOnMs = 1000;
  t.ringOffMs = 3000;
  t.rings = 2;
  t.pickupDelayMs = 600;
  t.clickMs = 30;
  t.settleMs = 300;
  return t;
}

void test_dtmf_table() {
  uint16_t lo = 0, hi = 0;
  TEST_ASSERT_TRUE(dtmf('1', lo, hi));
  TEST_ASSERT_EQUAL(697, lo);
  TEST_ASSERT_EQUAL(1209, hi);
  TEST_ASSERT_TRUE(dtmf('5', lo, hi));
  TEST_ASSERT_EQUAL(770, lo);
  TEST_ASSERT_EQUAL(1336, hi);
  TEST_ASSERT_TRUE(dtmf('9', lo, hi));
  TEST_ASSERT_EQUAL(852, lo);
  TEST_ASSERT_EQUAL(1477, hi);
  TEST_ASSERT_TRUE(dtmf('0', lo, hi));
  TEST_ASSERT_EQUAL(941, lo);
  TEST_ASSERT_EQUAL(1336, hi);
  TEST_ASSERT_FALSE(dtmf('a', lo, hi));
  TEST_ASSERT_FALSE(dtmf('?', lo, hi));
}

void test_sequence_shape() {
  Segment s[64];
  size_t n = build("123", timing(), s, 64);
  // lead, 3x(tone,gap), ring delay, ring, gap, ring, pickup delay, click, settle
  TEST_ASSERT_EQUAL(1 + 6 + 1 + 3 + 3, n);
  TEST_ASSERT_EQUAL(Kind::Silence, s[0].kind);
  TEST_ASSERT_EQUAL(300, s[0].ms);
  TEST_ASSERT_EQUAL(Kind::Tone, s[1].kind);
  TEST_ASSERT_EQUAL(697, s[1].f1);
  TEST_ASSERT_EQUAL(1209, s[1].f2);
  TEST_ASSERT_EQUAL(120, s[1].ms);
  TEST_ASSERT_EQUAL(Kind::Silence, s[2].kind);
  TEST_ASSERT_EQUAL(80, s[2].ms);
  TEST_ASSERT_EQUAL(1477, s[5].f2);  // '3'
  TEST_ASSERT_EQUAL(800, s[7].ms);   // ring delay
  TEST_ASSERT_EQUAL(Kind::Tone, s[8].kind);
  TEST_ASSERT_EQUAL(425, s[8].f1);
  TEST_ASSERT_EQUAL(0, s[8].f2);
  TEST_ASSERT_EQUAL(1000, s[8].ms);
  TEST_ASSERT_EQUAL(3000, s[9].ms);
  TEST_ASSERT_EQUAL(Kind::Tone, s[10].kind);
  TEST_ASSERT_EQUAL(600, s[11].ms);
  TEST_ASSERT_EQUAL(Kind::Noise, s[12].kind);
  TEST_ASSERT_EQUAL(30, s[12].ms);
  TEST_ASSERT_EQUAL(Kind::Silence, s[13].kind);
  TEST_ASSERT_EQUAL(300, s[13].ms);
}

void test_no_rings_and_zero_lengths_skipped() {
  Timing t = timing();
  t.rings = 0;
  t.leadMs = 0;
  t.settleMs = 0;
  Segment s[64];
  size_t n = build("7", t, s, 64);
  // tone, gap, ring delay, pickup delay, click
  TEST_ASSERT_EQUAL(5, n);
  TEST_ASSERT_EQUAL(Kind::Tone, s[0].kind);
  TEST_ASSERT_EQUAL(Kind::Noise, s[4].kind);
}

void test_invalid_digits_ignored_and_capacity_respected() {
  Segment s[4];
  size_t n = build("1x2", timing(), s, 4);
  TEST_ASSERT_EQUAL(4, n);
  TEST_ASSERT_EQUAL(697, s[1].f1);   // '1'
  TEST_ASSERT_EQUAL(1336, s[3].f2);  // '2', 'x' skipped
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dtmf_table);
  RUN_TEST(test_sequence_shape);
  RUN_TEST(test_no_rings_and_zero_lengths_skipped);
  RUN_TEST(test_invalid_digits_ignored_and_capacity_respected);
  return UNITY_END();
}
