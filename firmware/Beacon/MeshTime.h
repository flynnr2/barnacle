#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Ed25519.h>

namespace beacon {
enum ClockSource { UNSET, FALLBACK_ESTIMATE, MESH_ESTIMATE, USB_TIME, GNSS_TIME };

struct BeaconClock {
  uint32_t epoch = 0, tick = 0, lastStamp = 0;
  bool valid = false;
  ClockSource source = UNSET;

  void advance(uint32_t now) {
    uint32_t seconds = uint32_t(now - tick) / 1000;
    epoch += seconds; tick += seconds * 1000;
  }
  void startFallback(uint32_t build, uint32_t now) {
    epoch = build > lastStamp ? build : lastStamp;
    tick = now; source = FALLBACK_ESTIMATE; valid = true;
  }
  bool bootstrap(uint32_t value, uint32_t build, uint32_t now) {
    // A signature authenticates the sender, not its clock. This is only an estimate.
    if ((valid && source != FALLBACK_ESTIMATE) || value <= lastStamp || value < build - 86400UL ||
        uint64_t(value) > uint64_t(build) + 5ULL * 366 * 86400) return false;
    epoch = value; tick = now; source = MESH_ESTIMATE; valid = true; return true;
  }
  bool setTrusted(uint32_t value, ClockSource next, uint32_t now) {
    if (next != USB_TIME && next != GNSS_TIME) return false;
    if (valid && (source == USB_TIME || source == GNSS_TIME)) {
      if (next == USB_TIME && value < epoch) return false;
      if (next == GNSS_TIME) {
        uint32_t delta = value > epoch ? value - epoch : epoch - value;
        if ((source == USB_TIME && delta > 5) || (value < epoch && delta > 2)) return false;
      }
      if (value < epoch) value = epoch;
    }
    // Correct an estimate even backwards; nextStamp keeps emitted timestamps advancing.
    epoch = value; tick = now; source = next; valid = true; return true;
  }
  uint32_t nextStamp() const { return epoch > lastStamp ? epoch : lastStamp + 1; }
  bool canAdvertise() const { return valid && lastStamp < UINT32_MAX; }
};

struct StampRecord {
  uint32_t magic, timestamp, inverse;
  uint8_t identity[32];
  bool validFor(const uint8_t pub[32]) const {
    return magic == 0x48534231UL && inverse == ~timestamp &&
        timestamp >= 1700000000UL && timestamp <= 4102444800UL && !memcmp(identity, pub, 32);
  }
};

struct HeardAdvert {
  uint32_t timestamp;
  uint8_t identity[32], hops, type;
  char name[32];
};

enum ReceiveResult { ACCEPTED, OTHER_TRAFFIC, UNSUPPORTED_VERSION, BAD_FRAME,
                     BAD_ADVERT_LENGTH, SELF_ADVERT, BAD_SIGNATURE, BAD_APP, RECEIVE_RESULT_COUNT };
inline const char *receiveReason(ReceiveResult result) {
  static const char *names[] = {"signed-advert", "other-traffic", "version", "frame",
      "advert-length", "self-echo", "signature", "app-fields"};
  return names[result];
}

inline ReceiveResult inspectAdvert(const uint8_t *raw, size_t size, const uint8_t self[32], HeardAdvert &out) {
  if (size < 2 || size > 255) return BAD_FRAME;
  if ((raw[0] >> 6) != 0) return UNSUPPORTED_VERSION;
  uint8_t route = raw[0] & 3;
  size_t pathOffset = (route == 0 || route == 3) ? 5 : 1;
  if (pathOffset >= size) return BAD_FRAME;
  uint8_t path = raw[pathOffset], hashSize = (path >> 6) + 1;
  size_t pathBytes = size_t(path & 63) * hashSize;
  if (hashSize > 3 || pathBytes > 64) return BAD_FRAME;
  size_t payloadOffset = pathOffset + 1 + pathBytes;
  if (payloadOffset >= size || size - payloadOffset > 184) return BAD_FRAME;
  if (((raw[0] >> 2) & 15) != 4) return OTHER_TRAFFIC;
  if (size - payloadOffset < 100) return BAD_ADVERT_LENGTH;
  const uint8_t *payload = raw + payloadOffset, *app = payload + 100;
  size_t appSize = size - payloadOffset - 100;
  // Upstream Mesh::onRecvPacket caps signed app data at 32 bytes. A name is optional.
  if (appSize > 32) appSize = 32;
  uint8_t message[68];
  memcpy(message, payload, 36); memcpy(message + 36, app, appSize);
  if (!Ed25519::verify(payload + 36, payload, message, 36 + appSize)) return BAD_SIGNATURE;
  if (!memcmp(payload, self, 32)) return SELF_ADVERT;
  uint8_t flags = appSize ? app[0] : 0, type = flags & 15;
  size_t nameOffset = appSize ? 1 + ((flags & 0x10) ? 8 : 0) + ((flags & 0x20) ? 2 : 0) + ((flags & 0x40) ? 2 : 0) : 0;
  if (nameOffset > appSize) return BAD_APP;
  out.timestamp = uint32_t(payload[32]) | uint32_t(payload[33]) << 8 |
      uint32_t(payload[34]) << 16 | uint32_t(payload[35]) << 24;
  memcpy(out.identity, payload, 32); out.hops = path & 63; out.type = type;
  size_t len = (flags & 0x80) ? appSize - nameOffset : 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t c = app[nameOffset + i];
    out.name[i] = c >= 32 && c <= 126 ? c : '?';
  }
  out.name[len] = 0;
  if (!len) strcpy(out.name, "(unnamed)");
  return ACCEPTED;
}
inline bool readAdvert(const uint8_t *raw, size_t size, const uint8_t self[32], HeardAdvert &out) {
  return inspectAdvert(raw, size, self, out) == ACCEPTED;
}
}
