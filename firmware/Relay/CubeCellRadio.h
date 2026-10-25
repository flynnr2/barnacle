#pragma once
#include <LoRaWan_APP.h>
#include <sx126x.h>
#include "RadioPolicy.h"
#include "RelayPolicy.h"

// CubeCell's Radio.Sleep does not cancel its software TX timer. Cancel it when
// the reference dispatcher aborts a send, otherwise it can later knock RX down.
extern TimerEvent_t TxTimeoutTimer;

namespace relayfw {
struct RadioStats {
  volatile uint32_t received=0,crc=0,overflow=0,malformed=0,txTimeouts=0,recoveries=0;
};
class CubeCellRadio : public mesh::Radio {
  static CubeCellRadio *instance;
  RadioEvents_t events={};
  uint8_t incoming[255];
  volatile unsigned size=0;
  volatile bool pending=false,restart=false;
  volatile uint8_t completion=0;
  volatile int16_t irqRssi=0;
  volatile int8_t irqSnr=0;
  int16_t rssi=0;
  int8_t snr=0;
  bool transmitting=false;
  uint32_t agcTick=0,nonrxTick=0;
  barnacle::ReceiveActivity activity;
  static void rxDone(uint8_t *p,uint16_t n,int16_t r,int8_t s){
    auto &self=*instance;++self.stats.received;
    if(self.pending || n>sizeof(self.incoming)){++self.stats.overflow;return;}
    memcpy(self.incoming,p,n);self.size=n;self.irqRssi=r;self.irqSnr=s;self.pending=true;
  }
  static void rxError(){++instance->stats.crc;instance->restart=true;}
  static void rxTimeout(){instance->restart=true;}
  static void txDone(){instance->completion=1;}
  static void txTimeout(){++instance->stats.txTimeouts;instance->completion=2;}
  void listen(){
    if(!okay || transmitting)return;
    ::Radio.Standby();
    ::Radio.SetRxConfig(MODEM_LORA,3,7,1,0,32,0,false,0,true,false,0,false,true);
    ::Radio.SetPublicNetwork(false);::Radio.Write(REG_RX_GAIN,0x96);
    okay=::Radio.Read(REG_RX_GAIN)==0x96 && ::Radio.Read(0x0740)==0x14 && ::Radio.Read(0x0741)==0x24;
    if(!okay)return;
    SX126xClearIrqStatus(IRQ_RADIO_ALL);::Radio.Rx(0);
    const uint16_t done=IRQ_RX_DONE|IRQ_CRC_ERROR|IRQ_RX_TX_TIMEOUT;
    SX126xSetDioIrqParams(done|barnacle::ACTIVITY_BITS|IRQ_HEADER_ERROR,done,0,0);
    activity.reset();restart=false;nonrxTick=millis();
  }
  void configureRadio(){
    // Match the heard beacon: explicitly restore channel and TX state after
    // calibration, then restore private sync, boosted RX and IRQ activity.
    ::Radio.Sleep();::Radio.SetChannel(910525000);
    ::Radio.SetTxConfig(MODEM_LORA,22,0,3,7,1,32,false,true,0,0,false,3000);
    ::Radio.SetPublicNetwork(false);okay=::Radio.Read(0x0740)==0x14 && ::Radio.Read(0x0741)==0x24;
    listen();
  }
public:
  RadioStats stats;
  bool okay=false,lastSendOkay=false;
  bool busy() const {return transmitting;}
  bool hasPending() const {return pending;}
  void begin() override {
    instance=this;events.RxDone=rxDone;events.RxError=rxError;events.RxTimeout=rxTimeout;
    events.TxDone=txDone;events.TxTimeout=txTimeout;
    ::Radio.Init(&events);configureRadio();agcTick=millis();
  }
  void loop() override {
    ::Radio.IrqProcess();if(transmitting || pending || !okay)return;
    if(restart){listen();return;}
    if(isInRecvMode())nonrxTick=millis();
    else if(uint32_t(millis()-nonrxTick)>8000){++stats.recoveries;listen();}
    if(uint32_t(millis()-agcTick)>=16000 && !isReceiving()){
      ::Radio.Sleep();::Radio.Standby();CalibrationParams_t c={};c.Value=0x7f;SX126xCalibrate(c);
      SX126xCalibrateImage(910525000);SX126xSetDio2AsRfSwitchCtrl(true);
      configureRadio();agcTick=millis();
    }
  }
  int recvRaw(uint8_t *out,int max) override {
    if(!pending)return 0;
    unsigned n=size;
    bool valid=n<=unsigned(max) && validFrame(incoming,n);
    if(valid){memcpy(out,incoming,n);rssi=irqRssi;snr=irqSnr;}else ++stats.malformed;
    pending=false;return valid?int(n):0;
  }
  uint32_t getEstAirtimeFor(int n) override {return barnacle::airtimeMs(barnacle::BOSTON,n);}
  float packetScore(float s,int) override {return s/10.0f;}
  bool startSendRaw(const uint8_t *p,int n) override {
    // The dispatcher can time out its receive deferral. Never interrupt a
    // known receive just to force a queued transmission through.
    if(!okay || transmitting || pending || n<2 || n>255 || isReceiving())return false;
    ::Radio.Standby();activity.reset();completion=0;lastSendOkay=false;transmitting=true;
    ::Radio.Send(const_cast<uint8_t*>(p),uint8_t(n));return true;
  }
  bool isSendComplete() override {return transmitting && completion==1;}
  void onSendFinished() override {
    TimerStop(&TxTimeoutTimer);
    lastSendOkay=completion==1;transmitting=false;::Radio.Sleep();listen();agcTick=millis();
  }
  bool isInRecvMode() const override {return okay && !transmitting && ::Radio.GetStatus()==RF_RX_RUNNING;}
  bool isReceiving() override {
    if(pending)return true;if(transmitting || !okay)return false;
    uint16_t clear;bool value=activity.busy(SX126xGetIrqStatus(),millis(),barnacle::BOSTON,clear);
    if(clear)SX126xClearIrqStatus(clear);return value;
  }
  float getLastRSSI() const override{return rssi;}
  float getLastSNR() const override{return snr;}
};
CubeCellRadio *CubeCellRadio::instance=nullptr;
static_assert(IRQ_PREAMBLE_DETECTED==barnacle::PREAMBLE_BIT && IRQ_HEADER_VALID==barnacle::HEADER_BIT &&
  IRQ_HEADER_ERROR==barnacle::HEADER_ERROR_BIT && IRQ_RX_DONE==barnacle::RX_DONE_BIT &&
  IRQ_CRC_ERROR==barnacle::CRC_ERROR_BIT && IRQ_RX_TX_TIMEOUT==barnacle::TIMEOUT_BIT,"SDK IRQ meanings");
}
