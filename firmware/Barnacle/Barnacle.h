#pragma once
#include "Location.h"
#include "Wardrive.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

namespace barnacle {
static const char NAME[] = "HNTBarnacle";
static const uint32_t HOUR = 3600, ADVERT_SECONDS = 47UL * HOUR;
enum Profile { BOSTON = 1, UK = 2 };
enum Zone { BOSTON_TIME = 1, LONDON_TIME = 2 };
inline uint32_t frequency(Profile p) { return p == UK ? 869618000UL : 910525000UL; }
inline unsigned spreading(Profile p) { return p == UK ? 8 : 7; }
inline unsigned coding(Profile p) { return p == UK ? 4 : 1; } // SX126x: 1=4/5, 4=4/8.
inline const char *profileName(Profile p) { return p == UK ? "UK Narrow" : "Boston"; }

// Approximately square geographic cells, with a fixed longitude scale per
// latitude row. Rounding the cell centre to 0.001 degrees also makes the
// advertised microdegree fields visibly coarse (multiples of 1000).
inline bool coarse(double lat, double lon, unsigned metres, int32_t &a, int32_t &b) {
  if (!wardrive::validPosition(lat,lon) || fabs(lat) > 85 ||
      (metres != 250 && metres != 500 && metres != 1000)) return false;
  double step = metres / 111195.0;
  double centre = (floor((lat + 90) / step) + .5) * step - 90;
  double lngStep = step / cos(centre * .017453292519943295);
  double lngCentre = (floor((lon + 180) / lngStep) + .5) * lngStep - 180;
  if (lngCentre > 180) lngCentre = 180;
  a = int32_t(lround(centre * 1000)) * 1000;
  b = int32_t(lround(lngCentre * 1000)) * 1000;
  return true;
}
// CubeCell ASR650x wraps snprintf with a tiny formatter that does not support
// single-l length modifiers. Explicit 32-bit unsigned int arguments work on
// both the target and the host; Serial.printf uses a different formatter.
inline void coordinate(char *out, size_t size, int32_t micro) {
  uint32_t n = micro < 0 ? uint32_t(-int64_t(micro)) : uint32_t(micro);
  snprintf(out,size,"%s%u.%03u",micro < 0 ? "-" : "",(unsigned int)(n/1000000),(unsigned int)((n/1000)%1000));
}
struct Date { unsigned year, month, day; };
inline Date date(uint32_t utc) {
  uint32_t days = utc / 86400; Date d = {1970,1,1};
  while (days >= (beacon::leapYear(d.year) ? 366U : 365U)) days -= beacon::leapYear(d.year++) ? 366 : 365;
  const unsigned months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  while (days >= months[d.month-1] + (d.month==2 && beacon::leapYear(d.year))) {
    days -= months[d.month-1] + (d.month==2 && beacon::leapYear(d.year)); ++d.month;
  }
  d.day = days+1; return d;
}
inline unsigned sunday(unsigned year, unsigned month, unsigned which) {
  unsigned weekday = (beacon::utcEpoch(year,month,1,0,0,0)/86400+4)%7;
  return 1 + (7-weekday)%7 + (which-1)*7;
}
inline int offset(uint32_t utc, Zone p) {
  unsigned year = date(utc).year;
  if (p == LONDON_TIME) {
    unsigned mar = sunday(year,3,5); if (mar>31) mar-=7;
    unsigned oct = sunday(year,10,5); if (oct>31) oct-=7;
    return utc >= beacon::utcEpoch(year,3,mar,1,0,0) && utc < beacon::utcEpoch(year,10,oct,1,0,0) ? 3600 : 0;
  }
  return utc >= beacon::utcEpoch(year,3,sunday(year,3,2),7,0,0) &&
         utc < beacon::utcEpoch(year,11,sunday(year,11,1),6,0,0) ? -14400 : -18000;
}
inline uint32_t local(uint32_t utc, Zone p) { return uint32_t(int64_t(utc)+offset(utc,p)); }
inline const char *zone(uint32_t utc, Zone p) {
  int n=offset(utc,p); return p==LONDON_TIME ? (n ? "BST" : "GMT") : (n==-14400 ? "EDT" : "EST");
}
inline bool midnight(uint32_t utc, Zone p) { return local(utc,p)%86400 == 0; }
inline bool hourDue(uint32_t utc, uint32_t lastHour) {
  // Five-second service window for IRQ/flash work. No boot or catch-up burst.
  return utc/3600 > lastHour && utc%3600 <= 5;
}
inline uint32_t nextHour(uint32_t utc, uint32_t lastHour) {
  uint32_t h=utc/3600; return ((h>lastHour?h:lastHour)+1)*3600;
}
struct State {
  uint32_t magic, identity, epoch, lastHour, number, lastAdvert, boots;
  uint16_t grid;
  uint8_t enabled, profile;
  uint8_t zone, reserved[3];
  uint32_t check;
  void seal() { check=wardrive::checksum(this,sizeof(*this)-4); }
  bool valid(uint32_t key) const {
    return magic==0x484e4231 && identity==key && epoch>=1700000000UL && epoch<=4102444800UL &&
      lastHour<=4102444800UL/3600 &&
      (grid==250 || grid==500 || grid==1000) && enabled<=1 &&
      (profile==BOSTON || profile==UK) && (zone==BOSTON_TIME || zone==LONDON_TIME) &&
      check==wardrive::checksum(this,sizeof(*this)-4);
  }
  void begin(uint32_t key,uint32_t utc) {
    memset(this,0,sizeof(*this)); magic=0x484e4231; identity=key; epoch=utc;
    grid=1000; enabled=1; profile=BOSTON; zone=BOSTON_TIME; seal();
  }
};
static_assert(sizeof(State)==40,"preserve old summary and existing timestamp record");
}
