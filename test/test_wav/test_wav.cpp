#include <unity.h>

#include <string.h>
#include <vector>

#include "WavHeader.h"

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}
static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back((uint8_t)x);
  v.push_back((uint8_t)(x >> 8));
}
static void putTag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

static std::vector<uint8_t> makeWav(uint32_t rate, uint16_t ch, uint16_t bits, uint32_t dataBytes,
                                    bool withListChunk) {
  std::vector<uint8_t> v;
  putTag(v, "RIFF");
  put32(v, 0);
  putTag(v, "WAVE");
  putTag(v, "fmt ");
  put32(v, 16);
  put16(v, 1);
  put16(v, ch);
  put32(v, rate);
  put32(v, rate * ch * bits / 8);
  put16(v, ch * bits / 8);
  put16(v, bits);
  if (withListChunk) {
    putTag(v, "LIST");
    put32(v, 5);  // odd size -> pad byte
    v.insert(v.end(), {'I', 'N', 'F', 'O', 'x', 0});
  }
  putTag(v, "data");
  put32(v, dataBytes);
  return v;
}

void test_plain_header() {
  auto v = makeWav(16000, 1, 16, 32000, false);
  WavInfo info;
  TEST_ASSERT_TRUE(wav::parse(v.data(), v.size(), info));
  TEST_ASSERT_EQUAL_UINT32(16000, info.sampleRate);
  TEST_ASSERT_EQUAL(1, info.channels);
  TEST_ASSERT_EQUAL(16, info.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(32000, info.dataSize);
}

void test_header_with_list_chunk() {
  auto v = makeWav(22050, 2, 16, 100, true);
  WavInfo info;
  TEST_ASSERT_TRUE(wav::parse(v.data(), v.size(), info));
  TEST_ASSERT_EQUAL(2, info.channels);
  TEST_ASSERT_EQUAL_UINT32(v.size(), info.dataOffset);
}

void test_rejects_garbage() {
  const uint8_t junk[64] = {'I', 'D', '3'};
  WavInfo info;
  TEST_ASSERT_FALSE(wav::parse(junk, sizeof(junk), info));
  TEST_ASSERT_FALSE(wav::parse(junk, 4, info));
}

void test_rejects_unsupported_bits() {
  auto v = makeWav(16000, 1, 24, 100, false);
  WavInfo info;
  TEST_ASSERT_FALSE(wav::parse(v.data(), v.size(), info));
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_plain_header);
  RUN_TEST(test_header_with_list_chunk);
  RUN_TEST(test_rejects_garbage);
  RUN_TEST(test_rejects_unsupported_bits);
  return UNITY_END();
}
