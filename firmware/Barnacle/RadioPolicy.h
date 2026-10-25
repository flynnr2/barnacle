#pragma once
#include "Barnacle.h"

namespace barnacle {
// SX126x IRQ bits; the sketch checks these against the installed vendor header.
static const uint16_t RX_DONE_BIT=0x0002, PREAMBLE_BIT=0x0004, SYNC_BIT=0x0008,
  HEADER_BIT=0x0010, HEADER_ERROR_BIT=0x0020, CRC_ERROR_BIT=0x0040, TIMEOUT_BIT=0x0200;
static const uint16_t ACTIVITY_BITS=PREAMBLE_BIT|SYNC_BIT|HEADER_BIT;
static const uint32_t AGC_INTERVAL_MS=16000;
inline uint32_t airtimeMs(Profile p,unsigned bytes) {
  unsigned sf=spreading(p), numerator=8*bytes-4*sf+28+16;
  unsigned blocks=(numerator+4*sf-1)/(4*sf);
  unsigned symbols=8+blocks*(coding(p)+4);
  uint32_t symbolUs=(1UL<<sf)*16; // BW 62.5kHz; LDRO is off at SF7/SF8.
  return ((4*(32+symbols)+17)*symbolUs+3999)/4000;
}
struct ReceiveActivity {
  uint32_t since=0;
  uint8_t phase=0; // 0=idle, 1=preamble, 2=header/payload
  void reset(){phase=0;}
  bool busy(uint16_t irq,uint32_t now,Profile profile,uint16_t &clear) {
    clear=0;
    if(irq&(RX_DONE_BIT|HEADER_ERROR_BIT|CRC_ERROR_BIT|TIMEOUT_BIT)){
      if(irq&HEADER_ERROR_BIT)clear=ACTIVITY_BITS|HEADER_ERROR_BIT;
      reset();return false;
    }
    uint8_t next=(irq&HEADER_BIT)?2:(irq&(PREAMBLE_BIT|SYNC_BIT))?1:0;
    if(!next){reset();return false;}
    if(next!=phase){phase=next;since=now;}
    uint32_t limit=phase==2?airtimeMs(profile,255)+200:
      ((32+12)*(1UL<<spreading(profile))*16+999)/1000+100;
    if(uint32_t(now-since)<limit)return true;
    // Bound stale activity bits, as upstream does. Never force a transmit
    // merely because the hourly service window is about to close.
    clear=ACTIVITY_BITS;reset();return false;
  }
};
inline bool due(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
}
