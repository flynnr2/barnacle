#pragma once
#include "RelayPolicy.h"
#include "Profile.h"

namespace relayfw {
class RelayMesh : public mesh::Mesh {
  Policy &policy;
public:
  // Optional bench observer; never changes routing or starts a transmission.
  void (*trace)(const char*,const uint8_t*,unsigned,float,float)=nullptr;
  mesh::Packet *own=nullptr;
  uint32_t forwarded=0,ownDone=0,txFailures=0,signedAdverts=0,policyDrops=0,poolDrops=0;
  uint8_t ownKind=0,result=0;
  RelayMesh(mesh::Radio &r,mesh::MillisecondClock &ms,mesh::RNG &rng,mesh::RTCClock &rtc,
            mesh::PacketManager &packets,mesh::MeshTables &seen,Policy &p):
    mesh::Mesh(r,ms,rng,rtc,packets,seen),policy(p){}
  int calcRxDelay(float,uint32_t) const override{return 0;}
  uint32_t getRetransmitDelay(const mesh::Packet *p) override {
    uint32_t t=_radio->getEstAirtimeFor(p->getRawLength())/2;return getRNG()->nextInt(0,5*t+1);
  }
  uint32_t getDirectRetransmitDelay(const mesh::Packet *p) override {
    uint32_t t=_radio->getEstAirtimeFor(p->getRawLength())*3/10;return getRNG()->nextInt(0,5*t+1);
  }
  bool allowPacketForward(const mesh::Packet *p) override {
    bool value=profile::FORWARDING && policy.allows(p,self_id);if(!value)++policyDrops;return value;
  }
  void onAdvertRecv(mesh::Packet*,const mesh::Identity&,uint32_t,const uint8_t*,size_t) override {++signedAdverts;}
  void logRxRaw(float snr,float rssi,const uint8_t raw[],int n) override {
    if(!_mgr->getFreeCount())++poolDrops;if(trace)trace("RX",raw,n,rssi,snr);
  }
  void tracePacket(const char *kind,mesh::Packet *p) {
    if(trace){uint8_t raw[MAX_TRANS_UNIT];unsigned n=p->writeTo(raw);trace(kind,raw,n,0,0);}
  }
  void logTx(mesh::Packet *p,int) override {
    tracePacket(p==own?"TXO":"TXF",p);
    if(p==own){++ownDone;own=nullptr;result=1;}else ++forwarded;
  }
  void logTxFail(mesh::Packet *p,int) override {
    tracePacket(p==own?"FAILO":"FAILF",p);
    ++txFailures;if(p==own){own=nullptr;result=2;}
  }
  void sendOwnFlood(mesh::Packet *p) {
    own=p;sendFlood(p,uint32_t(0),2); // reference framing + duplicate marking
    for(int i=0;i<_mgr->getOutboundTotal();++i)if(_mgr->getOutboundByIdx(i)==p){
      _mgr->removeOutboundByIdx(i);_mgr->queueOutbound(p,250,_ms->getMillis());break;
    }
  }
  // Optional packets yield to newly queued forwarding, or expire after their
  // service window. A durably claimed slot is skipped, never retried.
  bool cancelOwn(bool expired) {
    if(!own || (!expired && _mgr->getOutboundTotal()<2))return false;
    for(int i=0;i<_mgr->getOutboundTotal();++i)if(_mgr->getOutboundByIdx(i)==own){
      releasePacket(_mgr->removeOutboundByIdx(i));own=nullptr;result=3;return true;
    }
    return false; // already in flight; let completion/timeout settle it
  }
  uint16_t faults() const{return _err_flags;}
};
}
