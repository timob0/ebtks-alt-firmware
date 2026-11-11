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
  assert(state==3 && transferId==1 && transferLocked);
  char rejected[100]{};
  for(unsigned i=0;i<sizeof(rejected)-1;++i) {
    const int ch=onReply();
    if(ch==0)break;
    rejected[i]=(char)ch;
  }
  assert(std::strstr(rejected,"PREVIOUS TRANSFER UNRESOLVED"));
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
  service(nullptr);
  assert(state==3 && !espRebootPending);
  while(onReply()){}
  assert(state==0 && transferLocked);
  onOpcode(4);
  assert(state==1 && operation==4);
  JsonObject dummy{};
  offer(dummy);
  assert(lastOfferMs==0); // CPRESET was NOT forwarded to the ESP32
  service(nullptr);
  assert(state==3);
  while(onReply()){}
  assert(state==0 && transferLocked);
  // Emulator of the HP polling bug fixed in CPM85 v0.06d:
  // a reply with only its two-byte phase prefix read still owns the
  // mailbox. Once the rest and final NUL are read, a new opcode works.
  state=2;operation=11;readIndex=0;replyLength=6;
  std::memcpy(response,"W|BUSY",6);
  assert(onReply()=='W' && onReply()=='|');
  onOpcode(11);
  assert(state==2 && readIndex==2); // old ROM would stall here
  while(onReply()){}
  assert(state==0);
  onOpcode(11);
  assert(state==1 && operation==11);
  onOpcode(0);
  std::puts("PASS: NUL-terminated HP status reply releases Teensy mailbox");
  std::puts("PASS: B2d Teensy start identity survives lost ACK, local timeout, and competing start");
}
