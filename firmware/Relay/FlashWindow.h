#pragma once
#include <stdint.h>

namespace relayfw {
// A missed slot defers work; it never authorizes a write on a busy radio/UART.
// No PPS means no safe predicted slot: optional claims/logs wait, forwarding does not.
inline bool flashWindow(uint32_t now,uint32_t rise,bool validRise,uint32_t lastByte,
                        bool uartEmpty,bool radioIdle) {
  return validRise && uartEmpty && radioIdle && uint32_t(now-rise)<=64 &&
    uint32_t(now-lastByte)>=33; // <=1.953 ms after PPS, >=1.007 ms UART quiet
}
inline uint32_t stampInterval(uint32_t fromRTC,uint16_t fromFraction,
                              uint32_t toRTC,uint16_t toFraction) {
  // Unsigned RTC difference handles its 32-bit wrap; fractions cover <one RTC tick.
  return uint32_t((uint64_t(uint32_t(toRTC-fromRTC))*15625+256)/512)+fromFraction-toFraction;
}
}
