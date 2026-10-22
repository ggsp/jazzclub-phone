#pragma once
// What the caller hears between "number recognised" and "someone answers":
// DTMF tones for the dialled digits, ringback, then a pickup click.
// Builds a list of tone/silence/noise segments; AudioPlayer renders them.
// Pure C++, host-testable.

#include <stddef.h>
#include <stdint.h>

namespace callprogress {

enum class Kind : uint8_t { Silence, Tone, Noise };

struct Segment {
  Kind kind;
  uint16_t f1;  // Hz (Tone); 0 for Silence / Noise
  uint16_t f2;  // second DTMF frequency, 0 for a single tone
  uint32_t ms;
};

struct Timing {
  uint16_t leadMs;         // silence before the first DTMF tone
  uint16_t dtmfOnMs;
  uint16_t dtmfOffMs;
  uint16_t ringDelayMs;    // silence between the last tone and the first ring
  uint16_t ringToneHz;
  uint16_t ringOnMs;
  uint16_t ringOffMs;
  uint8_t rings;
  uint16_t pickupDelayMs;  // silence after the last ring before the click
  uint16_t clickMs;
  uint16_t settleMs;       // silence after the click before the sample starts
};

// DTMF keypad frequencies for '0'..'9', '*', '#'. Returns false otherwise.
inline bool dtmf(char key, uint16_t& low, uint16_t& high) {
  static const uint16_t kRow[] = {697, 770, 852, 941};
  static const uint16_t kCol[] = {1209, 1336, 1477};
  int row, col;
  if (key >= '1' && key <= '9') {
    row = (key - '1') / 3;
    col = (key - '1') % 3;
  } else if (key == '0') {
    row = 3, col = 1;
  } else if (key == '*') {
    row = 3, col = 0;
  } else if (key == '#') {
    row = 3, col = 2;
  } else {
    return false;
  }
  low = kRow[row];
  high = kCol[col];
  return true;
}

// Fill `out` (capacity `cap`) with the sequence for `digits`. Returns the
// number of segments written. Zero-length segments are skipped; the list is
// truncated silently if `cap` is too small.
inline size_t build(const char* digits, const Timing& t, Segment* out, size_t cap) {
  size_t n = 0;
  auto add = [&](Kind k, uint16_t f1, uint16_t f2, uint32_t ms) {
    if (ms == 0 || n >= cap) return;
    out[n++] = Segment{k, f1, f2, ms};
  };
  add(Kind::Silence, 0, 0, t.leadMs);
  for (const char* p = digits ? digits : ""; *p; ++p) {
    uint16_t lo, hi;
    if (!dtmf(*p, lo, hi)) continue;
    add(Kind::Tone, lo, hi, t.dtmfOnMs);
    add(Kind::Silence, 0, 0, t.dtmfOffMs);
  }
  add(Kind::Silence, 0, 0, t.ringDelayMs);
  for (uint8_t i = 0; i < t.rings; ++i) {
    add(Kind::Tone, t.ringToneHz, 0, t.ringOnMs);
    if (i + 1 < t.rings) add(Kind::Silence, 0, 0, t.ringOffMs);
  }
  add(Kind::Silence, 0, 0, t.pickupDelayMs);
  add(Kind::Noise, 0, 0, t.clickMs);
  add(Kind::Silence, 0, 0, t.settleMs);
  return n;
}

}  // namespace callprogress
