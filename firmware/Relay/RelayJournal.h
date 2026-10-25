#pragma once
#include "FlashJournal.h"

namespace relayfw {
enum LogType { RELAY_BOOT=32, HOURLY_CLAIM, ADVERT_CLAIM, OWN_TX_DONE, OWN_TX_FAIL,
               SUMMARY, ERROR_SUMMARY, OWN_TX_SKIP, RELAY_STATE=63 };
struct Ledger {
  uint32_t lastHour=0,lastAdvert=0,lastStamp=0,epoch=0;
  uint8_t advertFormat=0; // 0=old unlocated advert, 1=coarse GNSS advert
};
// Existing checksummed FlashPage/SavedEvent wire format and exact rows 400..509.
// Each commit uses a NEW row, never rewriting the previous acknowledged claim.
// Every row carries the sticky scheduler state, so later circular overwrites
// cannot discard the only copy of a last-hour claim. EEPROM is read-only.
class Journal {
public:
  Ledger ledger;
  uint32_t generation=0,sequence=0,writes=0;
  unsigned index=0;
  bool okay=true;
  template<class IO> void restore(IO &io,uint32_t identity) {
    uint32_t stateGeneration=0,legacyStamp=0;
    for(unsigned i=0;i<wardrive::FLASH_PAGES;++i){
      wardrive::FlashPage p;io.read(i,p);if(!p.valid())continue;
      if(p.generation>generation){generation=p.generation;index=i;}
      for(unsigned j=0;j<p.count;++j){
        const auto &e=p.events[j];if(e.seq>sequence)sequence=e.seq;
        if((e.type==wardrive::ADVERT_START || e.type==wardrive::PROBE_START) && e.stamp>legacyStamp)legacyStamp=e.stamp;
        if(e.type==RELAY_STATE && e.peerHash==identity && (e.fixAge==1 || e.fixAge==2) && p.generation>stateGeneration){
          stateGeneration=p.generation;ledger.lastHour=uint32_t(e.lat);ledger.lastAdvert=uint32_t(e.lon);
          ledger.lastStamp=e.stamp;ledger.epoch=e.utc;
          ledger.advertFormat=e.fixAge==2?e.detail&1:0;
        }
      }
    }
    if(legacyStamp>ledger.lastStamp)ledger.lastStamp=legacyStamp;
  }
  template<class IO> bool commit(IO &io,uint32_t identity,uint32_t utc,uint8_t type,
                                 uint32_t a=0,uint32_t b=0,uint32_t c=0,uint8_t flags=0) {
    if(!okay || generation==UINT32_MAX || sequence>UINT32_MAX-2)return false;
    wardrive::FlashPage p={};p.magic=0x57444c32;p.generation=generation+1;p.count=2;
    auto &e=p.events[0];e.seq=sequence+1;e.utc=utc;e.type=type;e.stamp=a;e.peerHash=b;e.lat=int32_t(c);e.flags=flags;
    auto &s=p.events[1];s.seq=sequence+2;s.utc=utc;s.type=RELAY_STATE;s.peerHash=identity;s.fixAge=2;s.detail=ledger.advertFormat;
    s.lat=int32_t(ledger.lastHour);s.lon=int32_t(ledger.lastAdvert);s.stamp=ledger.lastStamp;
    p.seal();unsigned target=generation?(index+1)%wardrive::FLASH_PAGES:0;
    if(!io.write(target,p)){okay=false;return false;}
    wardrive::FlashPage check;io.read(target,check);
    if(!check.valid() || memcmp(&p,&check,sizeof(p))){okay=false;return false;}
    index=target;generation=p.generation;sequence=s.seq;ledger.epoch=utc;++writes;return true;
  }
};
struct HourlySchedule {
  uint32_t bootTick=0,lastSync=0;
  bool synced=false;
  bool trusted(uint32_t now) const {return synced && uint32_t(now-lastSync)<=6UL*3600000;}
  void expire(uint32_t now){if(synced && !trusted(now))synced=false;}
  bool due(uint32_t utc,uint32_t lastHour,uint32_t now) const {
    // No boot/catch-up burst, retry or messages carrying a build-time estimate.
    return uint32_t(now-bootTick)>=30000 && trusted(now) && utc/3600>lastHour && utc%3600<=5;
  }
};
}
