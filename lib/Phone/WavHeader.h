#pragma once
// Minimal RIFF/WAVE header parser (PCM only). Pure C++, host-testable.
// Walks the chunk list, so headers with LIST/fact chunks (ffmpeg output) work.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;  // byte offset of first sample
  uint32_t dataSize = 0;    // bytes of sample data
};

namespace wav {

inline uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Parse the first `len` bytes of a file. Returns true if a PCM "fmt " chunk
// and the start of a "data" chunk were both found within the buffer.
inline bool parse(const uint8_t* buf, size_t len, WavInfo& out) {
  if (len < 12) return false;
  if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
  bool haveFmt = false;
  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t* id = buf + pos;
    uint32_t size = le32(buf + pos + 4);
    size_t body = pos + 8;
    if (memcmp(id, "fmt ", 4) == 0) {
      if (body + 16 > len) return false;
      uint16_t format = le16(buf + body);
      if (format != 1 && format != 0xFFFE) return false;  // PCM / extensible
      out.channels = le16(buf + body + 2);
      out.sampleRate = le32(buf + body + 4);
      out.bitsPerSample = le16(buf + body + 14);
      haveFmt = true;
    } else if (memcmp(id, "data", 4) == 0) {
      if (!haveFmt) return false;
      out.dataOffset = (uint32_t)body;
      out.dataSize = size;
      return out.channels >= 1 && out.channels <= 2 && out.sampleRate > 0 &&
             (out.bitsPerSample == 16 || out.bitsPerSample == 8);
    }
    pos = body + size + (size & 1);  // chunks are word-aligned
  }
  return false;
}

}  // namespace wav
