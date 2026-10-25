#pragma once
// Host simulation of the vendor HAL only. Production never includes this file.
#include <Arduino.h>
#include <vector>
#include <cassert>
enum {MODEM_LORA=1,RF_IDLE=0,RF_RX_RUNNING=1,RF_TX_RUNNING=2,REG_RX_GAIN=0x08ac};
struct RadioEvents_t {
  void (*TxDone)()=nullptr;void (*TxTimeout)()=nullptr;
  void (*RxDone)(uint8_t*,uint16_t,int16_t,int8_t)=nullptr;
  void (*RxError)()=nullptr;void (*RxTimeout)()=nullptr;
};
struct TimerEvent_t {unsigned stops=0;};
static TimerEvent_t TxTimeoutTimer;
inline void TimerStop(TimerEvent_t *t){++t->stops;}
struct TestLowRadio {
  RadioEvents_t *events=nullptr;
  unsigned frequency=0,txPower=0,txBW=0,txSF=0,txCR=0,txPreamble=0,rxPreamble=0;
  unsigned channelWrites=0,txConfigWrites=0;
  int state=RF_IDLE;bool gainFault=false;uint8_t gain=0;bool priv=false;
  std::vector<std::vector<uint8_t>> sent;
  void Init(RadioEvents_t *e){events=e;}
  void SetChannel(unsigned value){frequency=value;++channelWrites;}
  void SetTxConfig(int,int power,int,int bw,int sf,int cr,int pre,bool fix,bool crc,int,int,bool iq,int){
    txPower=power;txBW=bw;txSF=sf;txCR=cr;txPreamble=pre;++txConfigWrites;assert(!fix && crc && !iq);
  }
  void SetRxConfig(int,int bw,int sf,int cr,int,int pre,int,bool fix,int,bool crc,bool,int,bool iq,bool continuous){
    assert(bw==3 && sf==7 && cr==1 && !fix && crc && !iq && continuous);rxPreamble=pre;
  }
  void SetPublicNetwork(bool value){priv=!value;}
  void Write(unsigned address,uint8_t value){if(address==REG_RX_GAIN)gain=value;}
  uint8_t Read(unsigned address){return address==REG_RX_GAIN?(gainFault?0:gain):address==0x0740?0x14:address==0x0741?0x24:0;}
  void Standby(){state=RF_IDLE;}
  void Sleep(){state=RF_IDLE;}
  void Rx(int){state=RF_RX_RUNNING;}
  void Send(uint8_t *p,uint8_t n){state=RF_TX_RUNNING;sent.emplace_back(p,p+n);}
  int GetStatus(){return state;}
  void IrqProcess(){}
};
static TestLowRadio Radio;
