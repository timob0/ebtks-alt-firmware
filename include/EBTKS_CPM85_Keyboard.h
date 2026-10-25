#pragma once
#include <Arduino.h>
#include "EBTKS_CPM85_Rmidle_Probe.h"

// CPM85 experimental HP-85 -> Teensy keyboard bridge, no AUXROM ROM banking.
// EBTKS I/O ports are 0341 (0177741) for raw key events and 0342 (0177742)
// for 1=attach / 0=detach. Only register on the development branch.
// The HP bus ISR calls the two handlers. Do not do UART, SD, logging,
// allocation or any lengthy work in this ISR-facing code.
namespace Cpm85Keys {
static constexpr uint8_t kRingSize=128; // one slot unused
static constexpr uint8_t kBatchMax=24;
static volatile uint8_t ring[kRingSize] = {};
static volatile uint8_t head=0, tail=0;
static volatile bool attached=false;
static volatile uint16_t dropped=0;

inline void reset() {
  head=tail=0;
  dropped=0;
}
inline void onControl(uint8_t value) {
  if(value!=0 && value!=1) return;
  if(value==1) {
    // A new attachment starts a new session, discarding stale data.
    reset();
    attached=true;
  } else {
    // K8 detach must not drop characters already captured for CP/M.
    attached=false;
  }
}
inline void onKey(uint8_t key) {
  if(!attached) return;
  uint8_t next=(head+1)&(kRingSize-1);
  if(next==tail) {
    ++dropped;
    return;
  }
  ring[head]=key;
  head=next;
}
// Called only in Teensy's regular main loop. IRQ masking avoids a detach
// racing with popping a queued key. Keep critical sections just a few cycles.
inline int pop() {
  noInterrupts();
  int result=-1;
  if(head!=tail) {
    result=ring[tail];
    tail=(tail+1)&(kRingSize-1);
  }
  interrupts();
  return result;
}
inline bool isAttached() {return attached;}
inline uint16_t overflowCount() {return dropped;}
inline uint8_t pending() {
  noInterrupts();
  uint8_t n=(head-tail)&(kRingSize-1);
  interrupts();
  return n;
}
} // namespace Cpm85Keys

inline void onWriteCpm85Key(uint8_t value){
  if (!Cpm85RmidleProbe::onKey(value)) Cpm85Keys::onKey(value);
}
inline void onWriteCpm85Control(uint8_t value){
  Cpm85RmidleProbe::onControl(value);
  Cpm85Keys::onControl(value);
}
