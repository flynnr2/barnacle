#pragma once
#include <stdint.h>
enum {IRQ_TX_DONE=1,IRQ_RX_DONE=2,IRQ_PREAMBLE_DETECTED=4,IRQ_SYNCWORD_VALID=8,IRQ_HEADER_VALID=16,
      IRQ_HEADER_ERROR=32,IRQ_CRC_ERROR=64,IRQ_RX_TX_TIMEOUT=512,IRQ_RADIO_ALL=65535};
struct CalibrationParams_t {uint8_t Value;};
static uint16_t testIrq=0,testIrqEnabled=0,testDio1=0;
static unsigned testCalibrations=0;
// Fault injection, not a claim about what calibration does on real hardware.
static bool testLoseTxConfigOnCalibrate=false;
inline uint16_t SX126xGetIrqStatus(){return testIrq;}
inline void SX126xClearIrqStatus(uint16_t bits){testIrq&=~bits;}
inline void SX126xSetDioIrqParams(uint16_t enabled,uint16_t dio1,int,int){testIrqEnabled=enabled;testDio1=dio1;}
inline void SX126xCalibrate(CalibrationParams_t){
  ++testCalibrations;
  if(testLoseTxConfigOnCalibrate){
    ::Radio.frequency=0;::Radio.txPower=0;::Radio.txBW=0;
    ::Radio.txSF=0;::Radio.txCR=0;::Radio.txPreamble=0;
  }
}
inline void SX126xCalibrateImage(unsigned){}
inline void SX126xSetDio2AsRfSwitchCtrl(bool){}
