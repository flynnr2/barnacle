#pragma once
#include <SHA512.h>
#include <Ed25519.h>
#include <helpers/AdvertDataHelpers.h>
#include "Barnacle.h"
#include "Profile.h"

namespace relayfw {
inline void importIdentity(mesh::LocalIdentity &id,const uint8_t seed[32]) {
  uint8_t blob[96];SHA512 hash;hash.update(seed,32);hash.finalize(blob,64);
  blob[0]&=248;blob[31]&=63;blob[31]|=64;Ed25519::derivePublicKey(blob+64,seed);
  id.readFrom(blob,sizeof(blob));memset(blob,0,sizeof(blob));
}
inline mesh::Packet *utcMessage(mesh::Mesh &relay,const mesh::GroupChannel &channel,
                                uint32_t utc,uint32_t stamp,bool holdover) {
  uint8_t plain[80];memcpy(plain,&stamp,4);plain[4]=0;auto d=barnacle::date(utc);
  int n=snprintf((char*)plain+5,sizeof(plain)-5,"%s: %04u-%02u-%02u %02u:00 UTC%s",profile::NAME,d.year,d.month,d.day,
    unsigned(utc/3600%24),holdover?" (holdover)":"");
  if(n<0 || unsigned(n)>=sizeof(plain)-5)return nullptr;
  return relay.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT,channel,plain,5+n);
}
inline mesh::Packet *advert(mesh::Mesh &relay,const uint8_t seed[32],uint32_t stamp,
                            bool located=false,int32_t latitude=0,int32_t longitude=0) {
  uint8_t message[68];AdvertDataBuilder data(profile::ADVERT_TYPE,profile::NAME);
  unsigned n=data.encodeTo(message+36);
  if(located){
    if(latitude%1000 || longitude%1000 || latitude< -90000000 || latitude>90000000 ||
       longitude< -180000000 || longitude>180000000)return nullptr;
    // Standard advert flags, int32 microdegree latitude/longitude, then name.
    // Inputs are already coarse cell centres; encode integers directly to avoid
    // introducing one-microdegree rounding residue through a double conversion.
    if(n+8>MAX_ADVERT_DATA_SIZE)return nullptr;
    uint8_t *app=message+36;memmove(app+9,app+1,n-1);app[0]|=ADV_LATLON_MASK;
    memcpy(app+1,&latitude,4);memcpy(app+5,&longitude,4);n+=8;
  }
  memcpy(message,relay.self_id.pub_key,32);memcpy(message+32,&stamp,4);
  auto p=relay.obtainNewPacket();if(!p)return nullptr;
  p->header=PAYLOAD_TYPE_ADVERT<<PH_TYPE_SHIFT;p->payload_len=100+n;
  memcpy(p->payload,message,36);memcpy(p->payload+100,message+36,n);
  Ed25519::sign(p->payload+36,seed,relay.self_id.pub_key,message,36+n);return p;
}
}
