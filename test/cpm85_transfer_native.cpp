// B2d HP mailbox ownership regression. The original START sequence must
// survive a missing ESP acknowledgement and a local HP timeout.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
static uint8_t readData=0;
static uint32_t fakeMillis=1;
uint32_t millis() {return fakeMillis;}
#include "EBTKS_CPM85_Drive.h"

int main() {
  using namespace Cpm85Drive;
  state=0;operation=0;nextSequence=0;sequence=0;
  argumentLength=0;readIndex=replyLength=0;
  transferId=0;transferLocked=false;lastOfferMs=0;
  onOpcode(9); // export START, pre-ACK identity is already fixed
  assert(state==1 && operation==9 && sequence==1);
  assert(transferId==1 && transferLocked);
  onOpcode(0); // lost ACK / local timeout must NOT cancel remote BDOS
  assert(state==0 && transferId==1 && transferLocked);
  onOpcode(10); // cannot start a new import and strand old export
  assert(state==0 && transferId==1 && transferLocked);
  onOpcode(11); // status query of the SAME original transfer
  assert(state==1 && operation==11 && sequence==2);
  assert(transferId==1);
  onOpcode(0);
  onOpcode(12); // only this opcode requests remote pre-start cancel
  assert(state==1 && operation==12 && sequence==3);
  assert(transferId==1 && transferLocked);
  onOpcode(0);
  assert(transferId==1 && transferLocked);
  // An unrelated CPREBOOT preserves the existing deferred hardware path
  // and cannot silently create another transfer transaction.
  onOpcode(8);
  assert(state==1 && operation==8 && transferId==1);
  std::puts("PASS: B2d Teensy start identity survives lost ACK, local timeout, and competing start");
}
