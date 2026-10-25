#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace wardrive {
static const uint32_t MOVING_MS = 30000, PARKED_MS = 300000, PROBE_S = 600;
inline bool validPosition(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}
static const uint32_t DURATION_S = 10800, MAX_PROBES = 18;
inline uint32_t checksum(const void *data, unsigned size) {
  uint32_t hash = 2166136261UL;
  const uint8_t *p = (const uint8_t *)data;
  while (size--) { hash ^= *p++; hash *= 16777619UL; }
  return hash;
}
struct Trip {
  uint32_t magic, identityHash, start, end, lastProbe, probes, active, check;
  void seal() { check = checksum(this, sizeof(*this) - 4); }
  bool valid(uint32_t identity) const {
    return magic == 0x57443131 && identityHash == identity && start >= 1700000000UL &&
      ((active <= 1 && end == start + DURATION_S) || (active == 2 && end == start)) && probes <= MAX_PROBES && active <= 2 &&
      check == checksum(this, sizeof(*this) - 4);
  }
  void begin(uint32_t now, uint32_t identity) {
    magic = 0x57443131; identityHash = identity; start = now; end = now + DURATION_S;
    lastProbe = now; probes = 0; active = 1; seal();
  }
  bool probeDue(uint32_t now, bool moving) const {
    return active == 1 && moving && now >= start && now < end && probes < MAX_PROBES &&
      now >= lastProbe && now - lastProbe >= PROBE_S;
  }
};
struct StartMotion {
  bool tracking = false;
  uint32_t since = 0, lastFix = 0;
  int32_t lat = 0, lon = 0;
  void reset() { tracking = false; }
  bool update(uint32_t now, bool fresh, int32_t latitude, int32_t longitude,
              bool speedValid, double speedMps) {
    if (!fresh || !speedValid || speedMps < 2.5) { tracking = false; return false; }
    if (!tracking || uint32_t(now - lastFix) >= 3000) {
      tracking = true; since = now; lat = latitude; lon = longitude;
    }
    lastFix = now;
    double north = double(latitude - lat) * .111195;
    double east = double(longitude - lon) * .111195 * cos(double(latitude) * 1e-6 * .017453292519943295);
    return uint32_t(now - since) >= 10000 && north * north + east * east >= 900;
  }
};
struct Motion {
  bool have = false, stationary = false;
  int32_t anchorLat = 0, anchorLon = 0;
  uint32_t lastMotion = 0, lastFix = 0;
  void update(uint32_t now, bool fresh, int32_t lat, int32_t lon, bool speedValid, double speedMps) {
    if (!fresh) { have = false; stationary = false; return; }
    // Missing fixes break the stationary evidence window.
    if (!have || uint32_t(now - lastFix) >= 15000) {
      have = true; stationary = false; anchorLat = lat; anchorLon = lon; lastMotion = now;
    }
    lastFix = now;
    double north = double(lat - anchorLat) * .111195;
    double east = double(lon - anchorLon) * .111195 * cos(double(lat) * 1e-6 * .017453292519943295);
    // 25 m deadband tolerates normal GNSS jitter. Speed >= 1.5 m/s resumes promptly.
    if (north * north + east * east >= 625 || (speedValid && speedMps >= 1.5)) {
      anchorLat = lat; anchorLon = lon; lastMotion = now; stationary = false;
    } else stationary = uint32_t(now - lastMotion) >= 120000;
  }
  uint32_t interval() const { return stationary ? PARKED_MS : MOVING_MS; }
};
enum EventType { BOOT, POSITION, ADVERT_START, PROBE_START, TX_DONE, TX_TIMEOUT,
                 RX, RX_CRC, FIX_LOST, STOPPED, RESUMED, TRIP_START, TRIP_END, STORAGE_ERROR, RX_DROP };
struct Event {
  uint32_t seq, utc, uptime, stamp;
  int32_t lat, lon;
  uint16_t fixAge;
  int16_t rssi;
  uint8_t type, clock, bytes, detail;
  int8_t snr;
  uint8_t fix, stationary, reserved;
};
static_assert(sizeof(Event) == 36, "bounded event size");
struct Journal {
  static const unsigned CAPACITY = 64;
  Event events[CAPACITY] = {};
  uint32_t sequence = 0, retained = 0;
  void add(Event e) { if (retained < CAPACITY) ++retained; e.seq = ++sequence; events[(e.seq - 1) % CAPACITY] = e; }
  uint32_t oldest() const { return sequence - retained + 1; }
};
}
