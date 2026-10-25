// Exercises the actual candidate headers and pinned reference dispatcher/core.
// All identities are synthetic; this test never reads the private device seed.
#include <cassert>
#include <cstdio>
#include <vector>
#include <array>
#include <random>
#include <fstream>
#include "RelayMesh.h"
#include "RelayMessages.h"
#include "RelayJournal.h"
#include "MeshTime.h"
#include "RelayClock.h"
#include "CubeCellRadio.h"

static uint32_t tick=1000;
uint32_t millis(){return tick;}
struct Clock : mesh::MillisecondClock,mesh::RTCClock {
  unsigned long getMillis() override{return ::tick;}
  uint32_t getCurrentTime() override{return 1792861200;}
  void setCurrentTime(uint32_t) override{}
};
struct Random : mesh::RNG {void random(uint8_t *p,size_t n) override{memset(p,0,n);}};
struct ProtocolRadio : mesh::Radio {
  std::vector<uint8_t> incoming;
  std::vector<std::vector<uint8_t>> sent;
  bool complete=true,receiving=false,fail=false;
  unsigned finishes=0;
  int recvRaw(uint8_t *p,int max) override {
    if(incoming.empty())return 0;assert(incoming.size()<=unsigned(max));
    unsigned n=incoming.size();memcpy(p,incoming.data(),n);incoming.clear();return n;
  }
  uint32_t getEstAirtimeFor(int) override{return 100;}
  float packetScore(float,int) override{return 1;}
  bool startSendRaw(const uint8_t *p,int n) override{if(fail)return false;sent.emplace_back(p,p+n);return true;}
  bool isSendComplete() override{return complete;}
  void onSendFinished() override{++finishes;}
  bool isInRecvMode() const override{return true;}
  bool isReceiving() override{return receiving;}
};
struct Manager : mesh::PacketManager {
  mesh::Packet pool[4];bool used[4]={};
  struct Entry {mesh::Packet *p;uint8_t priority;uint32_t due;};
  std::vector<Entry> queue;
  mesh::Packet* allocNew() override{for(unsigned i=0;i<4;++i)if(!used[i]){used[i]=true;pool[i]=mesh::Packet();return pool+i;}return nullptr;}
  void free(mesh::Packet *p) override{assert(p>=pool && p<pool+4 && used[p-pool]);used[p-pool]=false;}
  void queueOutbound(mesh::Packet *p,uint8_t priority,uint32_t due) override{queue.push_back({p,priority,due});assert(queue.size()<=4);}
  mesh::Packet *getNextOutbound(uint32_t now) override{
    int idx=-1;for(unsigned i=0;i<queue.size();++i)if(int32_t(now-queue[i].due)>=0 && (idx<0 || queue[i].priority<queue[idx].priority))idx=i;
    return idx<0?nullptr:removeOutboundByIdx(idx);
  }
  int getOutboundCount(uint32_t now) const override{int n=0;for(auto &e:queue)if(int32_t(now-e.due)>=0)++n;return n;}
  int getOutboundTotal() const override{return queue.size();}
  int getFreeCount() const override{int n=0;for(bool v:used)if(!v)++n;return n;}
  mesh::Packet *getOutboundByIdx(int i) override{return queue.at(i).p;}
  mesh::Packet *removeOutboundByIdx(int i) override{auto p=queue.at(i).p;queue.erase(queue.begin()+i);return p;}
  void queueInbound(mesh::Packet*,uint32_t) override{assert(false);}
  mesh::Packet *getNextInbound(uint32_t) override{return nullptr;}
};
struct Fixture {
  Clock clock;Random random;ProtocolRadio radio;Manager manager;relayfw::Seen seen;relayfw::Policy policy;
  relayfw::RelayMesh relay;
  Fixture():relay(radio,clock,random,clock,manager,seen,policy){policy.begin();memset(relay.self_id.pub_key,0xab,32);relay.begin();}
  void step(unsigned ms=1){tick+=ms;relay.loop();}
  void inject(mesh::Packet p){uint8_t b[255];unsigned n=p.writeTo(b);assert(relayfw::validFrame(b,n));radio.incoming.assign(b,b+n);step();step();step();}
};
static mesh::Packet group(unsigned nonce=1,unsigned route=1,unsigned width=2){
  mesh::Packet p;p.header=(PAYLOAD_TYPE_GRP_TXT<<PH_TYPE_SHIFT)|route;p.setPathHashSizeAndCount(width,0);
  p.payload_len=19;memset(p.payload,0x23,p.payload_len);p.payload[3]=nonce;return p;
}
static void forwarding(){
  for(unsigned width=1;width<=3;++width){
    Fixture f;auto p=group(1,1,width);f.inject(p);
    assert(f.radio.sent.size()==1 && f.relay.forwarded==1 && f.manager.getFreeCount()==4);
    mesh::Packet out;assert(out.readFrom(f.radio.sent[0].data(),f.radio.sent[0].size()));
    assert(out.getPathHashSize()==width && out.getPathHashCount()==1 && out.path[0]==0xab);
    f.inject(p);assert(f.radio.sent.size()==1 && f.seen.duplicates==1);
    // A repeat returned with a different path is the same payload/hash.
    p.setPathHashSizeAndCount(width,1);memset(p.path,0x12,width);f.inject(p);assert(f.radio.sent.size()==1);
  }
  for(unsigned key=0;key<3;++key){
    Fixture f;auto p=group(2,0);p.transport_codes[0]=f.policy.code(&p,key);p.transport_codes[1]=p.transport_codes[0];
    const uint16_t independent[]={0x24f2,0xa9bf,0xf451}; // Python hashlib/HMAC cross-check
    assert(p.transport_codes[0]==independent[key]);
    f.inject(p);assert(f.relay.forwarded==1);
  }
  {Fixture f;auto p=group(3,0);p.transport_codes[0]=1234;
   while(p.transport_codes[0]==f.policy.code(&p,0)||p.transport_codes[0]==f.policy.code(&p,1)||p.transport_codes[0]==f.policy.code(&p,2))++p.transport_codes[0];
   f.inject(p);assert(f.radio.sent.empty());}
  {Fixture f;auto p=group(4,0);p.transport_codes[0]=0;f.inject(p);assert(f.relay.forwarded==1);}
  for(unsigned width=1;width<=3;++width){
    Fixture f;auto p=group(5,2,width);p.setPathHashSizeAndCount(width,2);memset(p.path,0xab,width);memset(p.path+width,0x45,width);
    f.inject(p);assert(f.relay.forwarded==1);mesh::Packet out;out.readFrom(f.radio.sent[0].data(),f.radio.sent[0].size());
    assert(out.getPathHashCount()==1 && out.path[0]==0x45);
    p.payload[3]++;p.path[0]=0x44;f.inject(p);assert(f.relay.forwarded==1);
  }
  {Fixture f;auto p=group(6);p.setPathHashSizeAndCount(2,12);memset(p.path,0x12,24);f.inject(p);assert(f.radio.sent.empty());}
  {Fixture f;auto p=group(6,0,1);p.setPathHashSizeAndCount(1,63);memset(p.path,0x12,63);
   p.transport_codes[0]=f.policy.code(&p,0);f.inject(p);assert(f.radio.sent.empty());}
  for(unsigned width=1;width<=3;++width){
    Fixture f;auto p=group(7,1,width);unsigned repeats=width==1?2:1;p.setPathHashSizeAndCount(width,repeats);
    memset(p.path,0xab,width*repeats);f.inject(p);assert(f.radio.sent.empty());
  }
  // Four bounded queued packets, fifth dropped; all buffers recover after drain.
  {Fixture f;f.radio.receiving=true;for(unsigned i=0;i<5;++i)f.inject(group(10+i));
   assert(f.manager.getFreeCount()==0 && f.relay.poolDrops==1);f.radio.receiving=false;
   for(unsigned i=0;i<12;++i)f.step(500);assert(f.manager.getFreeCount()==4 && f.relay.forwarded==4);}
  {Fixture f;f.radio.complete=false;f.inject(group());assert(f.relay.forwarded==0);f.step(1000);
   assert(f.relay.txFailures==1 && f.relay.forwarded==0 && f.manager.getFreeCount()==4);}
  {Fixture f;f.radio.fail=true;f.inject(group());assert(f.relay.txFailures==1 && f.manager.getFreeCount()==4);}
  for(unsigned type : {PAYLOAD_TYPE_ACK,PAYLOAD_TYPE_MULTIPART,PAYLOAD_TYPE_TRACE}){
    Fixture f;mesh::Packet p;p.header=(type<<PH_TYPE_SHIFT)|ROUTE_TYPE_DIRECT;
    p.setPathHashSizeAndCount(2,1);memset(p.path,0xab,2);memset(p.payload,0,20);
    if(type==PAYLOAD_TYPE_TRACE){p.setPathHashSizeAndCount(1,0);p.payload_len=11;p.payload[8]=1;p.payload[9]=p.payload[10]=0xab;}
    else if(type==PAYLOAD_TYPE_ACK){p.payload_len=4;p.payload[0]=123;}
    else{p.payload_len=5;p.payload[0]=0x13;p.payload[1]=123;}
    f.inject(p);if(type==PAYLOAD_TYPE_MULTIPART){f.step(1000);f.step();}
    assert(f.relay.forwarded==1 && f.manager.getFreeCount()==4);
    f.inject(p);assert(f.relay.forwarded==1);
  }
  // Optional traffic yields to forwarding and releases its packet without TX.
  {Fixture f;auto p=f.relay.obtainNewPacket();*p=group();f.relay.sendOwnFlood(p);
   auto q=f.relay.obtainNewPacket();*q=group(8);f.relay.sendPacket(q,0);
   assert(f.relay.cancelOwn(false) && !f.relay.own && f.relay.result==3);f.step();f.step();
   assert(f.relay.forwarded==1 && f.relay.ownDone==0 && f.manager.getFreeCount()==4);}
  {Fixture f;auto p=f.relay.obtainNewPacket();*p=group();f.relay.sendOwnFlood(p);
   auto q=f.relay.obtainNewPacket();*q=group(8);f.relay.sendPacket(q,1);f.step();f.step();
   assert(f.relay.forwarded==1 && f.relay.ownDone==0);f.step();f.step();assert(f.relay.ownDone==1);}
  puts("PASS flood/direct routing, three hash widths/scopes, wildcard/unknown scope, dedup, hop/loop limits, queue overload, TX failure/timeout and own-message priority");
}
struct Flash {
  std::array<wardrive::FlashPage,wardrive::FLASH_PAGES> rows;
  bool fail=false,torn=false;
  Flash(){memset(rows.data(),0xff,sizeof(rows));}
  void read(unsigned i,wardrive::FlashPage &p){assert(i<rows.size());p=rows[i];}
  bool write(unsigned i,const wardrive::FlashPage &p){assert(i<rows.size());if(fail)return false;rows[i]=p;if(torn)rows[i].check^=1;return true;}
};
static void storage(const char *output){
  Flash io;relayfw::Journal j;j.restore(io,123);j.ledger.lastHour=42;j.ledger.lastStamp=151200;
  j.ledger.advertFormat=1;
  assert(j.commit(io,123,151200,relayfw::HOURLY_CLAIM));
  for(unsigned i=0;i<350;++i)assert(j.commit(io,123,151201+i,relayfw::SUMMARY));
  relayfw::Journal reboot;reboot.restore(io,123);assert(reboot.ledger.lastHour==42 && reboot.ledger.lastStamp==151200);
  assert(reboot.ledger.advertFormat==1);
  Flash legacy;legacy.rows[0]=io.rows[0];legacy.rows[0].events[1].fixAge=1;
  legacy.rows[0].events[1].detail=0xff;legacy.rows[0].seal();
  relayfw::Journal migrated;migrated.restore(legacy,123);assert(migrated.ledger.lastHour==42 && migrated.ledger.advertFormat==0);
  // Torn subsequent summary cannot destroy an already acknowledged hourly claim.
  io.torn=true;assert(!j.commit(io,123,152000,relayfw::SUMMARY));assert(!j.okay);
  relayfw::Journal recovered;recovered.restore(io,123);assert(recovered.ledger.lastHour==42);
  // A failed new claim never authorizes RF. Previous committed hour survives.
  io.fail=true;recovered.ledger.lastHour=43;assert(!recovered.commit(io,123,154800,relayfw::HOURLY_CLAIM));
  relayfw::Journal again;again.restore(io,123);assert(again.ledger.lastHour==42);
  relayfw::Journal stranger;stranger.restore(io,999);assert(stranger.ledger.lastHour==0);
  if(output){std::ofstream file(output,std::ios::binary);file.write((char*)io.rows.data(),sizeof(io.rows));assert(file.good());}
  relayfw::HourlySchedule s;s.bootTick=100;s.lastSync=100;s.synced=true;
  assert(!s.due(154800,42,200));assert(s.due(154800,42,30100));assert(!s.due(154806,42,30100));
  assert(!s.due(154800,43,30100));assert(!s.due(154800,42,7*3600000));
  s.expire(7*3600000);assert(!s.synced && !s.due(154800,42,30100));
  s.synced=true;s.lastSync=0xffff0000;s.bootTick=0xffff0000;assert(s.due(154800,42,0x1000));
  bool rollover=false;
  assert(beacon::normalizeGNSSTime(1792861200,1792850000,true,rollover)==1792861200 && !rollover);
  assert(!beacon::normalizeGNSSTime(1792861200,1792850000,false,rollover));
  assert(beacon::normalizeGNSSTime(1792861200-1024UL*7*86400,1792850000,true,rollover)==1792861200 && rollover);
  puts("PASS circular log recovery, sticky claims through 350 row rotations, torn writes, failed claims, identity binding, hourly/reset/holdover/millis-wrap policy");
}
static void messages(){
  Fixture f;uint8_t seed[32]={0x23};relayfw::importIdentity(f.relay.self_id,seed);
  auto p=relayfw::advert(f.relay,seed,1792861200);assert(p);
  uint8_t msg[68];unsigned n=p->payload_len-100;memcpy(msg,p->payload,36);memcpy(msg+36,p->payload+100,n);
  assert(f.relay.self_id.verify(p->payload+36,msg,36+n));AdvertDataParser data(p->payload+100,n);
  assert(data.isValid() && data.getType()==ADV_TYPE_REPEATER && !strcmp(data.getName(),"HNTBarnacle") && !data.hasLatLon());
  {Fixture receiver;p->header|=ROUTE_TYPE_FLOOD;p->setPathHashSizeAndCount(2,0);
   receiver.inject(*p);assert(receiver.relay.signedAdverts==1 && receiver.relay.forwarded==1);
   p->payload[40]^=1;receiver.inject(*p);assert(receiver.relay.signedAdverts==1 && receiver.relay.forwarded==1);}
  f.relay.releasePacket(p);
  // Located adverts use the existing one-kilometre cell-centre quantizer.
  // Different precise points in one cell must produce the same public point.
  int32_t lat=0,lon=0,lat2=0,lon2=0;
  assert(barnacle::coarse(51.501681,-0.109121,1000,lat,lon));
  assert(barnacle::coarse(51.501682,-0.109122,1000,lat2,lon2));
  assert(lat==lat2 && lon==lon2 && lat%1000==0 && lon%1000==0);
  p=relayfw::advert(f.relay,seed,1792861202,true,lat,lon);assert(p);
  n=p->payload_len-100;memcpy(msg,p->payload,36);memcpy(msg+36,p->payload+100,n);
  assert(f.relay.self_id.verify(p->payload+36,msg,36+n));AdvertDataParser located(p->payload+100,n);
  assert(located.isValid() && located.getType()==ADV_TYPE_REPEATER && located.hasLatLon());
  assert(located.getIntLat()==lat && located.getIntLon()==lon && !strcmp(located.getName(),"HNTBarnacle"));
  assert(lat!=51501681 && lon!= -109121);f.relay.releasePacket(p);
  assert(!relayfw::advert(f.relay,seed,1792861203,true,51501681,-109121));
  puts("PASS signed coarse-location advert/reference parser, one-kilometre cell stability, exact integer coordinates and precision guard");
  mesh::GroupChannel c={};mesh::Utils::sha256(c.secret,16,(const uint8_t*)"#test",5);mesh::Utils::sha256(c.hash,1,c.secret,16);
  for(bool hold : {false,true}){
    p=relayfw::utcMessage(f.relay,c,1792861200,1792861201,hold);assert(p && p->payload[0]==c.hash[0]);
    uint8_t plain[184]={};int len=mesh::Utils::MACThenDecrypt(c.secret,plain,p->payload+1,p->payload_len-1);
    assert(len>=5 && plain[4]==0);uint32_t stamp;memcpy(&stamp,plain,4);assert(stamp==1792861201);
    assert(strstr((char*)plain+5,"HNTBarnacle: 2026-10-24 17:00 UTC")== (char*)plain+5);
    assert(bool(strstr((char*)plain+5,"holdover"))==hold);f.relay.releasePacket(p);
  }
  // Real completion hook must count own TX separately.
  p=relayfw::utcMessage(f.relay,c,1792861200,1792861201,false);
  f.relay.sendOwnFlood(p);
  assert(p->getPathHashSize()==2 && p->getPathHashCount()==0);
  f.step();f.step();
  assert(f.relay.ownDone==1 && f.relay.forwarded==0 && f.relay.result==1 && !f.relay.own);
  puts("PASS candidate compact identity/repeater signature verified by reference crypto, #test decryption/text/holdover, own TX completion accounting");
}
static void disciplinedTime(){
  const uint32_t base=1792879200;
  assert(relayfw::usableFix(true,100,200,200,6,117));
  assert(!relayfw::usableFix(true,100,200,200,1,9999)); // captured weak-fix fault
  assert(!relayfw::usableFix(true,100,200,200,3,100));
  assert(!relayfw::usableFix(true,100,200,200,6,301));
  assert(!relayfw::usableFix(false,100,200,200,6,117));
  for(unsigned stale=0;stale<3;++stale)
    assert(!relayfw::usableFix(true,stale==0?2500:100,stale==1?2500:100,stale==2?2500:100,6,117));
  beacon::BeaconClock c;c.lastStamp=base+18;c.startFallback(base+18,0);
  relayfw::GNSSDiscipline d;
  for(unsigned i=0;i<10;++i){c.advance(i*1000);assert(d.observe(c,base+i,true,i*1000)==(i==9));}
  assert(c.epoch==base+9 && c.nextStamp()==base+19); // correct UTC, preserve packet nonce
  // Replay the observed +18s leap during one-satellite/HDOP99.99 operation.
  for(unsigned i=10;i<30;++i){c.advance(i*1000);assert(!d.observe(c,base+i+18,false,i*1000));assert(c.epoch==base+i);}
  // An isolated plausible-quality outlier cannot advance UTC either.
  c.advance(30000);assert(!d.observe(c,base+48,true,30000));assert(c.epoch==base+30);
  for(unsigned i=31;i<41;++i){c.advance(i*1000);d.observe(c,base+i,true,i*1000);}
  assert(c.epoch==base+40 && d.corrections==0);
  // Recovery from a previously accepted future clock needs 30 continuous seconds.
  c.epoch+=18;uint32_t stamp=c.nextStamp();c.lastStamp=stamp;
  d=relayfw::GNSSDiscipline();
  for(unsigned i=41;i<=71;++i){c.advance(i*1000);assert(d.observe(c,base+i,true,i*1000)==(i==71));}
  assert(c.epoch==base+71 && c.nextStamp()>stamp && d.corrections==1);
  relayfw::HourlySchedule schedule;schedule.synced=true;schedule.lastSync=71000;
  assert(!schedule.due(base,base/3600,71000)); // correction never repeats a claimed hour
  // Large date jumps and frozen timestamps cannot acquire a trusted clock.
  for(unsigned i=72;i<110;++i){c.advance(i*1000);assert(!d.observe(c,base+i+3600,true,i*1000));}
  relayfw::GNSSDiscipline frozen;beacon::BeaconClock f;f.startFallback(base,0);
  for(unsigned i=0;i<20;++i)assert(!frozen.observe(f,base,true,i*1000));
  relayfw::GNSSDiscipline wrap;beacon::BeaconClock w;w.startFallback(base,0xfffff000);
  for(unsigned i=0;i<10;++i){uint32_t now=0xfffff000+i*1000;w.advance(now);assert(wrap.observe(w,base+i,true,now)==(i==9));}
  assert(w.epoch==base+9);
  puts("PASS GNSS quality/stability, captured weak-fix +18s rejection, bidirectional UTC recovery, monotonic stamps/hour claims, frozen/date jumps and millis wrap");
}
static void malformed(){
  std::mt19937 random(42);Fixture f;
  for(unsigned n=0;n<256;++n)for(unsigned trial=0;trial<80;++trial){
    std::vector<uint8_t> raw(n);for(auto &v:raw)v=random();
    if(relayfw::validFrame(raw.data(),n)){mesh::Packet p;assert(f.relay.tryParsePacket(&p,raw.data(),n));}
  }
  uint8_t tiny[]={0,1,2,3,4};for(unsigned n=0;n<=5;++n)assert(!relayfw::validFrame(tiny,n));
  puts("PASS 20,480 bounded malformed/raw parser probes under address/undefined sanitizers");
}
static void radioAdapter(){
  relayfw::CubeCellRadio r;r.begin();assert(r.okay && r.isInRecvMode());
  assert(::Radio.frequency==910525000 && ::Radio.txPower==22 && ::Radio.txBW==3 && ::Radio.txSF==7 && ::Radio.txCR==1);
  assert(::Radio.txPreamble==32 && ::Radio.rxPreamble==32 && ::Radio.priv && ::Radio.gain==0x96);
  assert(testIrqEnabled&IRQ_PREAMBLE_DETECTED);assert(!(testDio1&IRQ_PREAMBLE_DETECTED));
  auto p=group();uint8_t raw[255],out[255];unsigned n=p.writeTo(raw);
  assert(r.startSendRaw(raw,n));assert(!r.isSendComplete() && r.busy());
  ::Radio.events->TxDone();assert(r.isSendComplete());r.onSendFinished();
  assert(r.lastSendOkay && r.isInRecvMode() && TxTimeoutTimer.stops);
  assert(r.startSendRaw(raw,n));::Radio.events->TxTimeout();assert(!r.isSendComplete());r.onSendFinished();
  assert(!r.lastSendOkay && r.isInRecvMode() && r.stats.txTimeouts==1);
  ::Radio.events->RxDone(raw,n,-119,-5);::Radio.events->RxDone(raw,n,-100,3);
  assert(r.stats.overflow==1 && !r.startSendRaw(raw,n));assert(r.recvRaw(out,255)==int(n));
  ::Radio.events->RxDone(raw,n,-100,3);assert(r.getLastRSSI()==-119 && r.getLastSNR()==-5);
  r.recvRaw(out,255);assert(r.getLastRSSI()==-100);
  ::Radio.events->RxDone(raw,1,-100,3);assert(!r.recvRaw(out,255) && r.stats.malformed==1);
  ::Radio.events->RxError();r.loop();assert(r.isInRecvMode() && r.stats.crc==1);
  testIrq=IRQ_PREAMBLE_DETECTED;assert(r.isReceiving() && !r.startSendRaw(raw,n));tick+=1000;
  assert(!r.isReceiving() && !testIrq);
  ::Radio.state=RF_IDLE;tick+=9000;r.loop();assert(r.isInRecvMode() && r.stats.recoveries==1);
  // Simulate lost TX state at calibration. Recovery must restore the whole
  // Boston profile rather than just RX settings or a healthy sync/gain flag.
  unsigned txWrites=::Radio.txConfigWrites,channelWrites=::Radio.channelWrites;
  testLoseTxConfigOnCalibrate=true;tick+=17000;r.loop();testLoseTxConfigOnCalibrate=false;
  assert(testCalibrations && r.isInRecvMode());
  assert(::Radio.txConfigWrites==txWrites+1 && ::Radio.channelWrites==channelWrites+1);
  assert(::Radio.frequency==910525000 && ::Radio.txPower==22 && ::Radio.txBW==3 && ::Radio.txSF==7 && ::Radio.txCR==1);
  assert(::Radio.txPreamble==32 && ::Radio.rxPreamble==32 && ::Radio.priv && ::Radio.gain==0x96);
  assert(r.startSendRaw(raw,n));::Radio.events->TxDone();r.onSendFinished();
  assert(r.lastSendOkay && r.isInRecvMode());
  ::Radio.gainFault=true;r.begin();assert(!r.okay && !r.startSendRaw(raw,n));::Radio.gainFault=false;
  puts("PASS actual radio adapter via simulated HAL: Boston config, IRQ activity, TX_DONE vs timeout, timer cancellation, RX buffering/metadata/CRC, stale activity, recovery, full TX configuration after AGC fault injection and gain fault");
}
#ifndef CUBECELL_PROFILE_TEST
int main(int argc,char **argv){forwarding();storage(argc>1?argv[1]:nullptr);messages();disciplinedTime();malformed();radioAdapter();}
#endif
