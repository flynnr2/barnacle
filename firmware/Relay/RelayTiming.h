#pragma once
#include <PWM1.h>
#include "FlashWindow.h"

extern uart_rxbuff_t _rxbuff[2];
namespace relayfw { namespace timing {
static volatile uint32_t bytes,lastByte,overruns,drops,frameErrors;
static volatile uint32_t riseRTC,rises,periodUs,widthUs,lateEdges;
static volatile uint32_t writes,maxWriteTicks,maxLoopTicks,deferrals,anchorErrors;
static uint32_t loopTick;
static volatile uint32_t edgeRTC,previousRiseRTC;
static volatile uint16_t edgeFraction,previousRiseFraction;
static volatile bool riseGood,edgeGood,edgeHigh,previousRiseGood,lateNext;
static bool timerGood;
static uint16_t capacity=UART_RX_SIZE;
static volatile uint16_t peak;
static volatile uart_rxbuff_t &ring(){return _rxbuff[1];}
static unsigned available(){return uint16_t((ring().rx_w+capacity-ring().rx_r)%capacity);}
static int read(){
  if(!available())return -1;uint8_t b=ring().rx_buf[ring().rx_r];
  ring().rx_r=(ring().rx_r+1)%capacity;return b;
}
static uint32_t raw(){return CySysTimerGetCount(2);}
static void rxISR(){
  uint32_t flags=UART_2_GetRxInterruptSource();
  if(flags&UART_2_INTR_RX_OVERFLOW)++overruns;
  if(flags&(UART_2_INTR_RX_FRAME_ERROR|UART_2_INTR_RX_PARITY_ERROR))++frameErrors;
  while(UART_2_SpiUartGetRxBufferSize()){
    uint32_t value=UART_2_UartGetByte();++bytes;lastByte=raw();
    uint16_t next=(ring().rx_w+1)%capacity;
    if(next==ring().rx_r)++drops;
    else{ring().rx_buf[ring().rx_w]=uint8_t(value);ring().rx_w=next;}
    unsigned depth=available();if(depth>peak)peak=depth;
  }
  UART_2_ClearRxInterruptSource(UART_2_INTR_RX_NOT_EMPTY|UART_2_INTR_RX_OVERFLOW|UART_2_INTR_RX_FRAME_ERROR|UART_2_INTR_RX_PARITY_ERROR);
}
static void ppsISR(){
  uint32_t entryRTC=raw(),entry=PWM1_COUNTER_REG,before=raw(),anchor;
  unsigned spins=1024;while((anchor=raw())==before && --spins){}
  uint32_t counter=PWM1_COUNTER_REG;
  bool good=timerGood && spins && !lateNext;
  if(!spins)++anchorErrors;if(lateNext)++lateEdges;lateNext=false;
  // Nominal 750 kHz counter interpolates only the short (<~40 us) anchor gap.
  uint16_t fraction=(uint32_t(uint16_t(counter-entry))*4+1)/3;
  bool high=digitalRead(GPIO8);
  if(high){
    periodUs=good && previousRiseGood && uint32_t(anchor-previousRiseRTC)<65536?stampInterval(previousRiseRTC,previousRiseFraction,anchor,fraction):0;
    previousRiseRTC=anchor;previousRiseFraction=fraction;previousRiseGood=good;
    riseRTC=entryRTC;riseGood=good;++rises;
  }else widthUs=good && edgeGood && edgeHigh?stampInterval(edgeRTC,edgeFraction,anchor,fraction):0;
  edgeRTC=anchor;edgeFraction=fraction;edgeGood=good;edgeHigh=high;
}
static void begin(){
  // Grow the already allocated SDK buffer once; custom reader and ISR must
  // share this size. Keep the original buffer on allocation failure.
  uint8_t *larger=(uint8_t*)malloc(1024),*old=nullptr;
  uint8_t irq=CyEnterCriticalSection();
  if(larger){
    unsigned n=available();for(unsigned i=0;i<n;++i)larger[i]=ring().rx_buf[(ring().rx_r+i)%capacity];
    old=ring().rx_buf;ring().rx_buf=larger;ring().rx_r=0;ring().rx_w=n;capacity=1024;
  }
  UART_2_SCB_IRQ_StartEx(rxISR);CyExitCriticalSection(irq);free(old);
  // Same counter/register setup validated by the temporary diagnostic; no output pin.
  PWM1_BLOCK_CONTROL_REG&=~PWM1_MASK;PWM1_CONTROL_REG=PWM1_PRESCALE_DIVBY16;
  PWM1_TRIG_CONTROL0_REG=0x10;PWM1_TRIG_CONTROL1_REG=0x0c;
  PWM1_PERIOD_REG=65535;PWM1_COUNTER_REG=0;PWM1_INTERRUPT_MASK_REG=0;
  PWM1_BLOCK_CONTROL_REG|=PWM1_MASK;PWM1_COMMAND_REG=PWM1_MASK<<PWM1_CMD_START;
  timerGood=((CY_GET_REG32(ClockPWM1__DIV_REGISTER)>>8)&65535)==3;
  pinMode(GPIO8,INPUT);attachInterrupt(GPIO8,ppsISR,CHANGE);loopTick=raw();
}
static void loop(){uint32_t now=raw(),gap=now-loopTick;loopTick=now;if(gap>maxLoopTicks)maxLoopTicks=gap;}
static bool ready(bool radioIdle){
  return flashWindow(raw(),riseRTC,riseGood && periodUs>=995000 && periodUs<=1005000,lastByte,
    !available() && !UART_2_SpiUartGetRxBufferSize(),radioIdle);
}
static void finishWrite(uint32_t start){
  uint32_t elapsed=raw()-start;++writes;if(elapsed>maxWriteTicks)maxWriteTicks=elapsed;
  if(CY_GET_REG32(CYREG_GPIO_PRT7_INTR)&1)lateNext=true;
}
static void status(){
  Serial.printf("TIMING pps=%lu period_us=%lu width_us=%lu late=%lu uart_overrun=%lu uart_drop=%lu uart_error=%lu flash=%lu flash_ticks=%lu loop_ticks=%lu wait=%lu anchor_error=%lu uart_capacity=%u uart_peak=%u\r\n",
    (unsigned long)rises,(unsigned long)periodUs,(unsigned long)widthUs,(unsigned long)lateEdges,
    (unsigned long)overruns,(unsigned long)drops,(unsigned long)frameErrors,(unsigned long)writes,
    (unsigned long)maxWriteTicks,(unsigned long)maxLoopTicks,(unsigned long)deferrals,(unsigned long)anchorErrors,unsigned(capacity-1),unsigned(peak));
}
}}
