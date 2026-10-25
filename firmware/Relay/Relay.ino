// HNTBarnacle relay-first candidate. Compile and host-test before authorized RF bench use.
#include <Arduino.h>
#include <Wire.h>
#include <GPS_Trans.h>
#include <HT_SSD1306Wire.h>
#include <SHA512.h>
#include <Ed25519.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/AdvertDataHelpers.h>
#include "BarnacleIdentity.h"
#include "build_info.h"
#include "MeshTime.h"
#include "RelayClock.h"
#include "RelayJournal.h"
#include "CubeCellRadio.h"
#include "RelayMesh.h"
#include "RelayMessages.h"

extern SSD1306Wire display;
extern "C" char _etext,__cy_region_init_size_ram;
static relayfw::CubeCellRadio rf;
static beacon::BeaconClock utc;
static relayfw::GNSSDiscipline discipline;
static TinyGPSPlus gps;
static relayfw::HourlySchedule schedule;
static relayfw::Journal journal;
static relayfw::Policy policy;
static relayfw::Seen seen;
static StaticPoolPacketManager packets(4);
static uint32_t identityHash,lastScreen,lastSummary,lastErrorLog,lastGNSSSentence,baudTick,lastStatus,loggedErrors,ownDeadline;
static unsigned baudIndex=0;
static bool gpsRollover=false;
static uint32_t traceUntil=0;
static bool gpsLocked=false,displayOK=false,flashSafe=false;
static unsigned dumpRow=wardrive::FLASH_PAGES;
static const uint32_t gnssBauds[]={9600,115200,38400,57600,19200,4800};
struct FlashIO {
  void read(unsigned i,wardrive::FlashPage &p){FLASH_read_at(wardrive::FLASH_LOG_START+i*256,(uint8_t*)&p,256);}
  bool write(unsigned i,const wardrive::FlashPage &p){
    return i<wardrive::FLASH_PAGES && FLASH_update(wardrive::FLASH_LOG_START+i*256,&p,256)==0;
  }
};
static FlashIO flash;
struct MS : mesh::MillisecondClock {unsigned long getMillis() override{return millis();}};
struct RTC : mesh::RTCClock {
  uint32_t getCurrentTime() override{return utc.epoch;}
  void setCurrentTime(uint32_t) override{} // Network packets cannot set GNSS time.
};
struct RNG : mesh::RNG {
  uint32_t state=1;
  void random(uint8_t *p,size_t n) override {
    while(n--){state^=state<<13;state^=state>>17;state^=state<<5;*p++=uint8_t(state);}
  }
};
static MS ms;static RTC rtc;static RNG rng;
static relayfw::RelayMesh relay(rf,ms,rng,rtc,packets,seen,policy);
static mesh::GroupChannel testChannel;

static bool idle(){return !rf.busy() && !rf.hasPending() && !packets.getOutboundTotal() && !rf.isReceiving();}
static bool goodGNSSFix(){
  return relayfw::usableFix(gps.location.isValid() && gps.satellites.isValid() && gps.hdop.isValid(),
    gps.location.age(),gps.satellites.age(),gps.hdop.age(),gps.satellites.value(),gps.hdop.value());
}
static bool coarseLocation(int32_t &latitude,int32_t &longitude){
  return goodGNSSFix() && discipline.samples>=10 &&
    barnacle::coarse(gps.location.lat(),gps.location.lng(),1000,latitude,longitude);
}
static bool checkpoint(uint8_t type,uint32_t a=0,uint32_t b=0,uint32_t c=0){
  return flashSafe && journal.commit(flash,identityHash,utc.epoch,type,a,b,c,uint8_t(utc.source));
}
static void restore(){
  uint32_t end=uint32_t(&_etext)+uint32_t(&__cy_region_init_size_ram),bootRow,length;
  const uint8_t *meta=(const uint8_t*)0x1ffc0;memcpy(&bootRow,meta+5,4);memcpy(&length,meta+9,4);
  flashSafe=end<=wardrive::FLASH_LOG_START && length>0 && (uint64_t(bootRow)+1)*256+length<=wardrive::FLASH_LOG_START;
  // Read legacy state directly, with no EEPROM allocation, commit or erase.
  barnacle::State old;beacon::StampRecord stamp;
  FLASH_read_at(CY_SFLASH_USERBASE+668,(uint8_t*)&old,sizeof(old));
  FLASH_read_at(CY_SFLASH_USERBASE+720,(uint8_t*)&stamp,sizeof(stamp));
  if(flashSafe)journal.restore(flash,identityHash);
  if(old.valid(identityHash)){
    if(old.lastHour>journal.ledger.lastHour)journal.ledger.lastHour=old.lastHour;
    if(old.epoch>journal.ledger.epoch)journal.ledger.epoch=old.epoch;
  }
  if(stamp.validFor(relay.self_id.pub_key) && stamp.timestamp>journal.ledger.lastStamp)journal.ledger.lastStamp=stamp.timestamp;
  utc.lastStamp=journal.ledger.lastStamp;
  uint32_t seed=journal.ledger.epoch>BUILD_EPOCH?journal.ledger.epoch:BUILD_EPOCH;
  utc.startFallback(seed,millis());
  if(flashSafe)checkpoint(relayfw::RELAY_BOOT,BUILD_EPOCH);
}
static void readGNSS(){
  uint32_t before=gps.passedChecksum();
  for(unsigned i=0;i<256 && GPSSerial.available();++i)gps.encode(GPSSerial.read());
  if(gps.passedChecksum()!=before){gpsLocked=true;lastGNSSSentence=millis();}
  if((!gpsLocked && uint32_t(millis()-baudTick)>=4000) ||
     (gpsLocked && uint32_t(millis()-lastGNSSSentence)>=15000)){
    gpsLocked=false;baudIndex=(baudIndex+1)%6;GPSSerial.updateBaudRate(gnssBauds[baudIndex]);baudTick=millis();
  }
  if(!gps.time.isUpdated())return;gps.time.value();
  if(!gps.time.isValid() || !gps.date.isValid() || gps.time.age()>=3000 || gps.date.age()>=3000)return;
  uint32_t raw=beacon::utcEpoch(gps.date.year(),gps.date.month(),gps.date.day(),gps.time.hour(),gps.time.minute(),gps.time.second());
  bool fix=goodGNSSFix();
  uint32_t value=beacon::normalizeGNSSTime(raw,BUILD_EPOCH,fix,gpsRollover);
  if(discipline.observe(utc,value,fix,millis())){schedule.synced=true;schedule.lastSync=millis();}
}
static bool claim(mesh::Packet *p,uint8_t kind,uint32_t stamp){
  // Signing/encryption took time; check for a receive before writing flash.
  if(!idle()){relay.releasePacket(p);return false;}
  journal.ledger.lastStamp=stamp;
  if(kind==1)journal.ledger.lastHour=utc.epoch/3600;
  else{journal.ledger.lastAdvert=utc.epoch;journal.ledger.advertFormat=1;}
  // Readback-verified durable claim MUST precede queueing any own transmission.
  if(!checkpoint(kind==1?relayfw::HOURLY_CLAIM:relayfw::ADVERT_CLAIM,stamp)){
    relay.releasePacket(p);return false;
  }
  utc.lastStamp=stamp;relay.ownKind=kind;
  ownDeadline=millis()+(kind==1?(6-utc.epoch%3600)*1000:5000);
  relay.sendOwnFlood(p);return true;
}
static void ownMessages(){
  if(dumpRow<wardrive::FLASH_PAGES || relay.own || relay.result || !utc.canAdvertise() || !flashSafe || !journal.okay || !rf.okay || !idle() || packets.getFreeCount()<2)return;
  if(relayfw::profile::HOURLY_TIME && schedule.due(utc.epoch,journal.ledger.lastHour,millis())){
    uint32_t stamp=utc.nextStamp();
    auto p=relayfw::utcMessage(relay,testChannel,utc.epoch,stamp,!goodGNSSFix() || uint32_t(millis()-schedule.lastSync)>=5000);
    if(p)claim(p,1,stamp);
  }else if(uint32_t(millis()-schedule.bootTick)>=30000 && schedule.trusted(millis()) &&
           (!journal.ledger.advertFormat || !journal.ledger.lastAdvert || (utc.epoch>=journal.ledger.lastAdvert && utc.epoch-journal.ledger.lastAdvert>=47UL*3600))){
    int32_t lat=0,lon=0;if(!coarseLocation(lat,lon))return;
    uint32_t stamp=utc.nextStamp();auto p=relayfw::advert(relay,IDENTITY_SEED,stamp,true,lat,lon);if(!p)return;
    claim(p,2,stamp);
  }
}
static uint32_t totalErrors(){return relay.txFailures+rf.stats.crc+rf.stats.overflow+rf.stats.malformed+rf.stats.recoveries+relay.poolDrops;}
static const char *clockLabel(){return !schedule.trusted(millis())?"WAIT":goodGNSSFix() && uint32_t(millis()-schedule.lastSync)<5000?"GNSS":"HOLD";}
static void hex(const uint8_t *p,unsigned n){
  const char *digits="0123456789abcdef";while(n--){Serial.write(digits[*p>>4]);Serial.write(digits[*p++&15]);}Serial.println();
}
static void packetTrace(const char *kind,const uint8_t *raw,unsigned n,float rssi,float snr){
  if(!traceUntil || barnacle::due(millis(),traceUntil))return;
  Serial.printf("TRACE kind=%s utc=%lu ms=%lu rssi=%d snr=%d data=",kind,(unsigned long)utc.epoch,(unsigned long)millis(),int(rssi),int(snr));hex(raw,n);
}
static void status(){
  Serial.printf("RELAY build=%s utc=%lu time=%s radio=%u storage=%u forwarded=%lu own_done=%lu rx=%lu signed=%lu duplicates=%lu queued=%u free=%u tx_fail=%lu crc=%lu overflow=%lu malformed=%lu recovery=%lu flags=%u journal_gen=%lu last_hour=%lu\r\n",
    BUILD_ID,(unsigned long)utc.epoch,clockLabel(),rf.okay,flashSafe && journal.okay,(unsigned long)relay.forwarded,
    (unsigned long)relay.ownDone,(unsigned long)rf.stats.received,(unsigned long)relay.signedAdverts,(unsigned long)seen.duplicates,
    packets.getOutboundTotal(),packets.getFreeCount(),(unsigned long)relay.txFailures,(unsigned long)rf.stats.crc,
    (unsigned long)rf.stats.overflow,(unsigned long)rf.stats.malformed,(unsigned long)rf.stats.recoveries,relay.faults(),
    (unsigned long)journal.generation,(unsigned long)journal.ledger.lastHour);
  Serial.printf("GNSS fix=%u sats=%lu hdop_x100=%lu fix_age_ms=%lu time_age_ms=%lu nmea=%lu failed=%lu baud=%lu rollover_1024w=%u sync_age_ms=%lu\r\n",
    goodGNSSFix(),(unsigned long)gps.satellites.value(),(unsigned long)gps.hdop.value(),
    (unsigned long)gps.location.age(),(unsigned long)gps.time.age(),(unsigned long)gps.passedChecksum(),(unsigned long)gps.failedChecksum(),
    (unsigned long)gnssBauds[baudIndex],gpsRollover,(unsigned long)(millis()-schedule.lastSync));
  Serial.printf("CLOCK candidate=%lu samples=%u rejected=%lu corrections=%lu\r\n",(unsigned long)discipline.candidate,
    discipline.samples,(unsigned long)discipline.rejected,(unsigned long)discipline.corrections);
  Serial.printf("HEALTH oled=%u radio_rx=%u policy_drops=%lu pool_drops=%lu tx_timeouts=%lu sync=%02x%02x gain=%02x trace=%u\r\n",
    displayOK,rf.isInRecvMode(),(unsigned long)relay.policyDrops,(unsigned long)relay.poolDrops,(unsigned long)rf.stats.txTimeouts,
    ::Radio.Read(0x0740),::Radio.Read(0x0741),::Radio.Read(REG_RX_GAIN),traceUntil && !barnacle::due(millis(),traceUntil));
  Serial.print("PUBLIC_KEY ");hex(relay.self_id.pub_key,32);
  int32_t lat=0,lon=0;bool fix=coarseLocation(lat,lon);
  Serial.printf("LOCATION grid_m=1000 fresh=%u lat_micro=%ld lon_micro=%ld advert_format=%u\r\n",fix,(long)lat,(long)lon,journal.ledger.advertFormat);
}
static void countLabel(char *out,size_t size,uint32_t value){
  if(value<10000)snprintf(out,size,"%u",unsigned(value));
  else if(value<10000000)snprintf(out,size,"%uk",unsigned(value/1000));
  else snprintf(out,size,"%uM",unsigned(value/1000000));
}
static void screen(bool force=false){
  if(!displayOK || (!force && (uint32_t(millis()-lastScreen)<5000 || !idle())))return;lastScreen=millis();
  char line[40];display.clear();snprintf(line,sizeof(line),"%s %s",relayfw::profile::NAME,relayfw::profile::OLED_ROLE);display.drawString(0,0,line);
  display.drawString(0,10,!rf.okay?"RADIO FAULT":!journal.okay || !flashSafe?"LOG FAULT":rf.isInRecvMode()?"Listening / ready":"Recovering RX");
  char a[8],b[8];countLabel(a,sizeof(a),relay.forwarded);countLabel(b,sizeof(b),rf.stats.received);
  snprintf(line,sizeof(line),"Fwd %s RX %s",a,b);display.drawString(0,20,line);
  countLabel(a,sizeof(a),relay.ownDone);
  snprintf(line,sizeof(line),"Q %u Free %u Own %s",packets.getOutboundTotal(),packets.getFreeCount(),a);display.drawString(0,30,line);
  countLabel(a,sizeof(a),totalErrors());countLabel(b,sizeof(b),seen.duplicates);
  snprintf(line,sizeof(line),"Err %s Dup %s",a,b);display.drawString(0,40,line);
  if(schedule.trusted(millis()))snprintf(line,sizeof(line),"%02u:%02u UTC %s",unsigned(utc.epoch/3600%24),unsigned(utc.epoch/60%60),clockLabel());
  else snprintf(line,sizeof(line),"--:-- UTC GNSS wait");
  display.drawString(0,50,line);display.display();
}
// Read-only serial commands. Dump one row per idle loop; no RF/admin/erase commands.
static char command[20];static unsigned commandLen=0;static bool overflow=false;
static void serialService(){
  for(unsigned i=0;i<32 && Serial.available();++i){
    char c=Serial.read();if(c=='\r' || c=='\n'){
      command[commandLen]=0;
      if(!overflow && commandLen){
        if(!strcmp(command,"status"))status();
        else if(!strcmp(command,"flash dump"))dumpRow=0;
        else if(!strcmp(command,"screen")){
          if(displayOK && idle()){
            Wire.beginTransmission(0x3c);Serial.printf("OLED ack=%u width=128 height=64\r\n",Wire.endTransmission()==0);
            Serial.print("SCREEN ");hex(display.buffer,1024);
          }else Serial.println("ERR screen unavailable or radio busy");
        }else if(!strcmp(command,"storage")){
          if(!idle())Serial.println("ERR radio busy");else{
            Serial.print("EEPROM ");const uint8_t *p=(const uint8_t*)CY_SFLASH_USERBASE;hex(p,764);
          }
        }else if(!strcmp(command,"trace on")){traceUntil=millis()+900000;Serial.println("TRACE enabled_ms=900000 RF_schedule_unchanged");}
        else if(!strcmp(command,"trace off")){traceUntil=0;Serial.println("TRACE disabled");}
        else Serial.println("ERR status | flash dump | screen | storage | trace on/off (diagnostics only)");
      }
      commandLen=0;overflow=false;
    }else if(commandLen<sizeof(command)-1 && !overflow)command[commandLen++]=c;else overflow=true;
  }
  if(dumpRow<wardrive::FLASH_PAGES && idle()){
    wardrive::FlashPage p;flash.read(dumpRow,p);Serial.printf("FLASH_ROW index=%u data=",dumpRow++);
    const char *digits="0123456789abcdef";const uint8_t *b=(const uint8_t*)&p;
    for(unsigned i=0;i<sizeof(p);++i){Serial.write(digits[b[i]>>4]);Serial.write(digits[b[i]&15]);}Serial.println();
    if(dumpRow==wardrive::FLASH_PAGES)Serial.println("FLASH_END");
  }
}
void setup(){
  Serial.begin(115200);pinMode(GPIO14,OUTPUT);digitalWrite(GPIO14,LOW);pinMode(Vext,OUTPUT);digitalWrite(Vext,LOW);
  GPSSerial.begin(9600);delay(300);displayOK=display.init();
  Wire.beginTransmission(0x3c);displayOK=displayOK && Wire.endTransmission()==0;
  if(displayOK){display.setTextAlignment(TEXT_ALIGN_LEFT);display.setContrast(80);}
  relayfw::importIdentity(relay.self_id,IDENTITY_SEED);
  relay.trace=packetTrace;
  identityHash=wardrive::checksum(relay.self_id.pub_key,32);policy.begin();restore();
  if(relayfw::profile::HOURLY_TIME){
    mesh::Utils::sha256(testChannel.secret,16,(const uint8_t*)"#test",5);
    mesh::Utils::sha256(testChannel.hash,1,testChannel.secret,16);
  }
  relay.begin();rng.state=::Radio.Random();if(!rng.state)rng.state=1;
  // Radio.Random may change the chip state; return to known configured RX.
  rf.begin();schedule.bootTick=millis();lastSummary=millis();lastErrorLog=millis();
  Serial.printf("%s; profile=%s; Boston; scopes bos,northeast,east; read-only serial\r\n",relayfw::profile::NAME,relayfw::profile::ROLE);status();screen(true);
}
void loop(){
  ::Radio.IrqProcess();utc.advance(millis());readGNSS();schedule.expire(millis());
  relay.cancelOwn(barnacle::due(millis(),ownDeadline));relay.loop();
  if(idle() && dumpRow==wardrive::FLASH_PAGES && flashSafe && journal.okay){
    if(relay.result){checkpoint(relay.result==1?relayfw::OWN_TX_DONE:relay.result==2?relayfw::OWN_TX_FAIL:relayfw::OWN_TX_SKIP,relay.ownKind,journal.ledger.lastStamp);relay.result=0;}
    else if(uint32_t(millis()-lastSummary)>=900000){
      checkpoint(relayfw::SUMMARY,relay.forwarded,rf.stats.received,totalErrors());lastSummary=millis();
    }else if(totalErrors()!=loggedErrors && uint32_t(millis()-lastErrorLog)>=900000){
      checkpoint(relayfw::ERROR_SUMMARY,relay.txFailures,rf.stats.crc,rf.stats.overflow+rf.stats.malformed+relay.poolDrops);
      lastErrorLog=millis();loggedErrors=totalErrors();
    }
  }
  ownMessages();serialService();screen();
  if(uint32_t(millis()-lastStatus)>=60000 && idle()){status();lastStatus=millis();}
  delay(1);
}
