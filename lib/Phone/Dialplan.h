#pragma once
// Maps a dialled digit string to a destination id, digit by digit.
//
// lookup() is called after every digit:
//   Match      - the dialled string equals an entry (or, with acceptAnyFull,
//                has reached maxDigits): connect to `id`.
//   Incomplete - some entry still starts with what was dialled: keep going.
//   NoMatch    - nothing can match any more: unobtainable.
//
// Entries may have different lengths ("9" and "123" can coexist). With
// acceptAnyFull, every maxDigits-long number connects with id = its numeric
// value, so N digits address 10^N destinations without listing them.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Dialplan {
 public:
  struct Entry {
    const char* number;
    uint16_t id;
  };
  enum class Result : uint8_t { Incomplete, Match, NoMatch };

  Dialplan(const Entry* entries, size_t count, uint8_t maxDigits, bool acceptAnyFull = false)
      : entries_(entries), count_(count), maxDigits_(maxDigits), acceptAny_(acceptAnyFull) {}

  uint8_t maxDigits() const { return maxDigits_; }
  void setEntries(const Entry* entries, size_t count) {
    entries_ = entries;
    count_ = count;
  }
  size_t entryCount() const { return count_; }

  Result lookup(const char* dialed, uint16_t* idOut) const {
    size_t len = strlen(dialed);
    bool prefixExists = false;
    for (size_t i = 0; i < count_; ++i) {
      const char* n = entries_[i].number;
      if (strcmp(n, dialed) == 0) {
        if (idOut) *idOut = entries_[i].id;
        return Result::Match;
      }
      if (strncmp(n, dialed, len) == 0 && strlen(n) > len) prefixExists = true;
    }
    if (acceptAny_ && len >= maxDigits_) {
      if (idOut) *idOut = numericValue(dialed);
      return Result::Match;
    }
    if (len >= maxDigits_) return Result::NoMatch;
    if (prefixExists || acceptAny_) return Result::Incomplete;
    return Result::NoMatch;
  }

 private:
  static uint16_t numericValue(const char* s) {
    uint32_t v = 0;
    for (; *s; ++s) {
      if (*s < '0' || *s > '9') return 0;
      v = v * 10 + (uint32_t)(*s - '0');
      if (v > 0xFFFF) return 0xFFFF;
    }
    return (uint16_t)v;
  }

  const Entry* entries_;
  size_t count_;
  uint8_t maxDigits_;
  bool acceptAny_;
};
