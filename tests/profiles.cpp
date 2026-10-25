// Synthetic identities only. Exercise forwarding through the actual Mesh core.
#define CUBECELL_PROFILE_TEST
#include "relay.cpp"

int main(){
  for(unsigned width=1;width<=3;++width){
    Fixture flood;flood.inject(group(41,1,width));
    assert(flood.relay.forwarded==(relayfw::profile::FORWARDING?1u:0u));
    assert(flood.radio.sent.size()==(relayfw::profile::FORWARDING?1u:0u));
    assert(flood.manager.getFreeCount()==4);
    Fixture direct;auto p=group(42,2,width);p.setPathHashSizeAndCount(width,2);
    memset(p.path,0xab,width);memset(p.path+width,0x45,width);direct.inject(p);
    assert(direct.relay.forwarded==(relayfw::profile::FORWARDING?1u:0u));
    assert(direct.manager.getFreeCount()==4);
  }
  Fixture f;uint8_t seed[32];memset(seed,0x71,sizeof(seed));
  relayfw::importIdentity(f.relay.self_id,seed);
  auto advert=relayfw::advert(f.relay,seed,1792861200,true,51500000,-120000);
  assert(advert);AdvertDataParser parsed(advert->payload+100,advert->payload_len-100);
  assert(parsed.isValid() && parsed.getType()==relayfw::profile::ADVERT_TYPE);
  assert(!strcmp(parsed.getName(),relayfw::profile::NAME));f.relay.releasePacket(advert);
  mesh::GroupChannel channel;mesh::Utils::sha256(channel.secret,16,(const uint8_t*)"#test",5);
  mesh::Utils::sha256(channel.hash,1,channel.secret,16);
  auto message=relayfw::utcMessage(f.relay,channel,1792861200,1792861200,false);assert(message);
  f.relay.sendOwnFlood(message);f.step(251);f.step();f.step();
  assert(f.relay.ownDone==1 && f.relay.forwarded==0);
  mesh::Packet sent;assert(sent.readFrom(f.radio.sent.back().data(),f.radio.sent.back().size()));
  assert(sent.getPathHashSize()==2 && sent.getPathHashCount()==0);
  printf("PASS profile %s: flood/direct forwarding=%u, advert role=%u, own packets retain two-byte paths\n",
    relayfw::profile::ROLE,relayfw::profile::FORWARDING,relayfw::profile::ADVERT_TYPE);
}
