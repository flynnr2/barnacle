#pragma once
#include <stdint.h>
#include <math.h>

namespace beacon {
inline bool freshLocation(bool valid, uint32_t ageMs) { return valid && ageMs < 15000; }
inline bool trustedGNSSTime(uint32_t candidate, uint32_t buildEpoch, bool freshFix) {
  // Some Air530 units emit checksum-valid default dates before acquisition.
  return freshFix && candidate >= buildEpoch - 86400UL &&
      candidate <= buildEpoch + 5UL * 366 * 86400;
}

inline uint32_t normalizeGNSSTime(uint32_t raw, uint32_t buildEpoch, bool freshFix, bool &rollover) {
  rollover = false;
  if (trustedGNSSTime(raw, buildEpoch, freshFix)) return raw;
  if (!freshFix || !raw || raw >= buildEpoch - 86400UL) return 0;
  // Observed Air530: 2007-03-08 becomes 2026-10-22 after one legacy GPS week rollover.
  uint64_t corrected = uint64_t(raw) + 1024ULL * 7 * 86400;
  if (corrected <= UINT32_MAX && trustedGNSSTime(uint32_t(corrected), buildEpoch, true)) {
    rollover = true; return uint32_t(corrected);
  }
  return 0;
}

// Uniform sampling on a sphere. No real GNSS position is an input.
inline void geoRandomPosition(const uint8_t entropy[8], int32_t &lat, int32_t &lon) {
  uint32_t a = 0, b = 0;
  for (int i = 0; i < 4; ++i) { a |= uint32_t(entropy[i]) << (8 * i); b |= uint32_t(entropy[4 + i]) << (8 * i); }
  double u = (double(a) + 0.5) / 4294967296.0;
  double v = (double(b) + 0.5) / 4294967296.0;
  lat = (int32_t)lround(asin(2 * u - 1) * (180 / 3.14159265358979323846) * 1000000);
  lon = (int32_t)lround((360 * v - 180) * 1000000);
}

inline bool leapYear(unsigned year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline uint32_t utcEpoch(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
  const uint8_t months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (year < 1980 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > months[month - 1] + (month == 2 && leapYear(year)) || hour > 23 || minute > 59 || second > 59) return 0;
  uint32_t days = 0;
  for (unsigned y = 1970; y < year; ++y) days += leapYear(y) ? 366 : 365;
  for (unsigned m = 1; m < month; ++m) days += months[m - 1] + (m == 2 && leapYear(year));
  days += day - 1;
  return days * 86400UL + hour * 3600UL + minute * 60UL + second;
}
}
