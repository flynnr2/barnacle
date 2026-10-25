#pragma once
#include "MeshTime.h"

namespace relayfw {
inline bool usableFix(bool valid,uint32_t locationAge,uint32_t satellitesAge,
                      uint32_t hdopAge,uint32_t satellites,uint32_t hdop) {
  return valid && locationAge<2500 && satellitesAge<2500 && hdopAge<2500 &&
    satellites>=4 && hdop>0 && hdop<=300;
}

// Discipline wall time separately from the durable, monotonic packet stamp.
// Weak/stale fixes never refresh holdover. A single good-looking time jump
// cannot move the clock; a bounded correction needs 30 seconds of agreement.
struct GNSSDiscipline {
  uint32_t candidate=0,sample=0,sampleTick=0,startTick=0,rejected=0,corrections=0;
  uint8_t samples=0;
  bool observe(beacon::BeaconClock &clock,uint32_t value,bool quality,uint32_t now) {
    candidate=value;
    if(!quality || !value){samples=0;sample=0;++rejected;return false;}
    if(sample==value && samples){
      if(uint32_t(now-sampleTick)>1500){samples=0;sample=0;++rejected;}
      return false;
    }
    uint32_t elapsed=now-sampleTick;
    bool continuous=samples && value>sample && value-sample==1 && elapsed>=500 && elapsed<=1500;
    if(!continuous){samples=1;startTick=now;}else if(samples<255)++samples;
    sample=value;sampleTick=now;
    bool trusted=clock.source==beacon::GNSS_TIME;
    uint32_t delta=value>clock.epoch?value-clock.epoch:clock.epoch-value;
    bool jump=trusted && delta>2;
    if((trusted && delta>120) || samples<(jump?31:10) || uint32_t(now-startTick)<(jump?30000UL:9000UL)){
      ++rejected;return false;
    }
    if(jump)++corrections;
    // Correct UTC in either direction. lastStamp and journal.lastHour are
    // intentionally untouched: no replayed nonce, advert stamp or hourly slot.
    clock.epoch=value;clock.tick=now;clock.source=beacon::GNSS_TIME;clock.valid=true;
    return true;
  }
};
}
