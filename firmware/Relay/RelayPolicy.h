#pragma once
#include <Mesh.h>
#include <SHA256.h>
#include <helpers/RoutingPolicy.h>

namespace relayfw {
// Boston guidance checked 2026-10-24. Keys are SHA256('#' + region)[:16].
struct Policy {
  uint8_t keys[3][16];
  void begin() {
    const char *names[]={"#bos","#northeast","#east"};
    for(unsigned i=0;i<3;++i)mesh::Utils::sha256(keys[i],16,(const uint8_t*)names[i],strlen(names[i]));
  }
  uint16_t code(const mesh::Packet *p,unsigned i) const {
    SHA256 hash;uint16_t value;uint8_t type=p->getPayloadType();
    hash.resetHMAC(keys[i],16);hash.update(&type,1);hash.update(p->payload,p->payload_len);
    hash.finalizeHMAC(keys[i],16,&value,2);
    return value==0?1:value==0xffff?0xfffe:value;
  }
  bool allows(const mesh::Packet *p,const mesh::Identity &self) const {
    // The wire count has six bits; never let appending hop 64 spill into the
    // path-width bits (including TRACE's special one-byte SNR path).
    if(p->getPayloadType()==PAYLOAD_TYPE_TRACE && p->path_len>=63)return false;
    if(!p->isRouteFlood())return true;
    if(p->getPathHashCount()>=63)return false;
    if(mesh::isFloodHopLimitExceeded(p,64,12,8))return false;
    if(p->hasTransportCodes() && p->transport_codes[0]){
      bool match=false;for(unsigned i=0;i<3;++i)if(code(p,i)==p->transport_codes[0])match=true;
      if(!match)return false;
    }
    // Reference moderate loop detection; one-byte collisions need tolerance.
    const uint8_t limits[]={0,2,1,1};unsigned repeats=0,width=p->getPathHashSize();
    if(width>3)return false;
    for(unsigned i=0;i<p->getPathHashCount();++i)
      if(self.isHashMatch(p->path+i*width,width))++repeats;
    return repeats<limits[width];
  }
};

// Bound every raw read before entering the reference parser. Reject truncated
// type-specific headers too: the reference core assumes these fields exist.
inline bool validFrame(const uint8_t *raw,unsigned len) {
  if(len<2 || len>MAX_TRANS_UNIT || raw[0]>>6)return false;
  unsigned route=raw[0]&3,i=(route==0 || route==3)?5:1;
  if(i>=len)return false;
  unsigned path=raw[i++],width=(path>>6)+1,bytes=(path&63)*width;
  if(width>3 || bytes>MAX_PATH_SIZE || i+bytes>=len)return false;
  i+=bytes;unsigned size=len-i,type=(raw[0]>>2)&15;
  if(size>MAX_PACKET_PAYLOAD)return false;
  switch(type){
    case PAYLOAD_TYPE_ACK:return size>=4;
    case PAYLOAD_TYPE_ADVERT:return size>=100;
    case PAYLOAD_TYPE_REQ:case PAYLOAD_TYPE_RESPONSE:case PAYLOAD_TYPE_TXT_MSG:case PAYLOAD_TYPE_PATH:
      return size>=2+CIPHER_MAC_SIZE+CIPHER_BLOCK_SIZE && (size-2-CIPHER_MAC_SIZE)%CIPHER_BLOCK_SIZE==0;
    case PAYLOAD_TYPE_ANON_REQ:
      return size>=1+PUB_KEY_SIZE+CIPHER_MAC_SIZE+CIPHER_BLOCK_SIZE && (size-1-PUB_KEY_SIZE-CIPHER_MAC_SIZE)%CIPHER_BLOCK_SIZE==0;
    case PAYLOAD_TYPE_GRP_TXT:case PAYLOAD_TYPE_GRP_DATA:
      return size>=1+CIPHER_MAC_SIZE+CIPHER_BLOCK_SIZE && (size-1-CIPHER_MAC_SIZE)%CIPHER_BLOCK_SIZE==0;
    case PAYLOAD_TYPE_TRACE:{
      if(size<9 || (route!=2 && route!=3))return false;
      unsigned entry=1U<<(raw[i+8]&3),off=(path&63)*entry,remaining=size-9;
      return off>=remaining || remaining-off>=entry;
    }
    case PAYLOAD_TYPE_MULTIPART:return size>=5 && (raw[i]&15)==PAYLOAD_TYPE_ACK;
    case PAYLOAD_TYPE_CONTROL:case PAYLOAD_TYPE_RAW_CUSTOM:return size>=1;
    default:return false;
  }
}

// Same hash and replacement policy as SimpleMeshTables, bounded to 96 entries.
class Seen : public mesh::MeshTables {
  uint8_t hashes[96][MAX_HASH_SIZE]={};
  unsigned next=0,count=0;
public:
  uint32_t duplicates=0;
  bool wasSeen(const mesh::Packet *p) override {
    uint8_t h[MAX_HASH_SIZE];p->calculatePacketHash(h);
    for(unsigned i=0;i<count;++i)if(!memcmp(h,hashes[i],sizeof(h))){++duplicates;return true;}
    return false;
  }
  void markSeen(const mesh::Packet *p) override {
    p->calculatePacketHash(hashes[next]);next=(next+1)%96;if(count<96)++count;
  }
  void clear(const mesh::Packet *p) override {
    uint8_t h[MAX_HASH_SIZE];p->calculatePacketHash(h);
    for(unsigned i=0;i<count;++i)if(!memcmp(h,hashes[i],sizeof(h))){memset(hashes[i],0,sizeof(h));break;}
  }
};
}
