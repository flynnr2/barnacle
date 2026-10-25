#pragma once
#include "Wardrive.h"
#include <string.h>

namespace wardrive {
// Heltec ASR650X Flash example permits rows 34..509; 510..511 are reserved.
// Build-time CYACD/bootloader-metadata checks AND a runtime image-end check
// ensure no executable bytes share this range. No writes to bootloader/user rows.
static const uint32_t FLASH_LOG_START = 0x19000, FLASH_LOG_END = 510 * 256;
static const unsigned FLASH_PAGES = (FLASH_LOG_END - FLASH_LOG_START) / 256;
struct SavedEvent {
  uint32_t seq, utc;
  int32_t lat, lon;
  uint32_t stamp, peerHash;
  int16_t rssi;
  uint16_t fixAge;
  int8_t snr;
  uint8_t type, flags, detail;
  static SavedEvent from(const Event &e, uint32_t peer = 0) {
    SavedEvent s = {};
    s.seq=e.seq; s.utc=e.utc; s.lat=e.lat; s.lon=e.lon; s.stamp=e.stamp; s.peerHash=peer;
    s.rssi=e.rssi; s.fixAge=e.fixAge; s.snr=e.snr; s.type=e.type;
    s.flags=(e.clock & 7) | (e.fix ? 8 : 0) | (e.stationary ? 16 : 0); s.detail=e.detail;
    return s;
  }
};
static_assert(sizeof(SavedEvent) == 32, "compact persistent event");
struct FlashPage {
  uint32_t magic, generation, count, check;
  uint32_t reserved[4];
  SavedEvent events[7];
  void seal() {
    check=0; check=checksum(this, sizeof(*this));
  }
  bool valid() const {
    if (magic != 0x57444c32 || !generation || count < 1 || count > 7) return false;
    FlashPage copy=*this; uint32_t expected=copy.check; copy.check=0;
    return expected == checksum(&copy, sizeof(copy));
  }
};
static_assert(sizeof(FlashPage) == 256, "one independently checked flash row");
// IO interface permits destructive-interruption testing against simulated flash.
// append rewrites only the current row; at most seven events share a power-loss risk.
// Other valid rows remain readable. Circular overwrite retains the newest rows.
class FlashJournal {
public:
  FlashPage page = {};
  unsigned index = 0, validPages = 0;
  uint32_t newestGeneration = 0, total = 0, writes = 0, overwrittenRows = 0;
  bool okay = true;
  template<class IO> void restore(IO &io) {
    page = {}; index = 0; validPages = 0; newestGeneration = total = 0;
    for (unsigned i=0; i<FLASH_PAGES; ++i) {
      FlashPage candidate; io.read(i, candidate);
      if (candidate.valid()) {
        ++validPages; total += candidate.count;
        if (candidate.generation > newestGeneration) {
          newestGeneration = candidate.generation; page = candidate; index=i;
        }
      }
    }
  }
  template<class IO> bool append(IO &io, const SavedEvent &event) {
    if (!okay || newestGeneration == UINT32_MAX) return false;
    if (!page.count || page.count == 7) {
      if (page.count) index=(index+1)%FLASH_PAGES;
      FlashPage previous; io.read(index,previous);
      if (previous.valid()) { total -= previous.count; ++overwrittenRows; }
      else ++validPages;
      memset(&page,0,sizeof(page)); page.magic=0x57444c32; page.generation=++newestGeneration;
    }
    page.events[page.count++]=event; page.seal(); ++total;
    return write(io);
  }
  template<class IO> bool finishTX(IO &io, uint32_t seq, bool success) {
    // RX callbacks only queue buffers while TX is active, so its record remains
    // in the current page until this completion is processed by the main loop.
    for (unsigned i=0; i<page.count; ++i) if (page.events[i].seq == seq) {
      page.events[i].detail |= success ? 0x80 : 0x40; page.seal(); return write(io);
    }
    okay=false; return false;
  }
private:
  template<class IO> bool write(IO &io) {
    ++writes; io.write(index,page); FlashPage readback; io.read(index,readback);
    okay = readback.valid() && !memcmp(&page,&readback,sizeof(page));
    return okay;
  }
};
}
