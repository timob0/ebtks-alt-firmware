#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
static uint32_t fakeMillis=1;
uint32_t millis() { return fakeMillis; }
// EBTKS's I/O read wrappers publish the byte through this shared bus latch.
static uint8_t readData=0;
#include "EBTKS_CPM85_Drive.h"

static unsigned resets=0;
static void restartEsp() { ++resets; }

static void prepare() {
  using namespace Cpm85Drive;
  state=0;operation=0;sequence=0;readIndex=0;
  replyLength=0;argumentLength=0;nextSequence=0;
  espRebootPending=false;espRebootTimerActive=false;
  espRebootStartMs=0;lastOfferMs=0;resets=0;fakeMillis=1;
}
int main() {
  using namespace Cpm85Drive;
  prepare();
  // The existing Z80-only reset remains a relayed operation.
  onOpcode(4);
  assert(state==1 && operation==4);
  service(restartEsp);
  assert(state==1 && resets==0);
  onOpcode(0); // HP aborted a pending command.
  assert(state==0 && resets==0);

  // No unknown opcodes accidentally enable the reset mechanism.
  onOpcode(9);
  assert(state==0 && !espRebootPending);

  // Local opcode eight is handled even if the ESP never polls the UART.
  onOpcode(8);
  assert(state==1 && operation==8);
  JsonObject dummy{};
  offer(dummy);
  assert(lastOfferMs==0);
  service(restartEsp);
  assert(state==2 && resets==0);
  // Interrupting before the reply has been fully consumed must NOT reboot.
  fakeMillis+=10000;
  service(restartEsp);
  assert(resets==0 && !espRebootPending);
  char answer[100]{};
  unsigned len=0;
  for(;;) {
    const uint8_t c=onReply();
    if(c==0)break;
    assert(len<sizeof(answer)-1);
    answer[len++]=(char)c;
  }
  assert(std::strcmp(answer,kEspRebootReply)==0);
  assert(state==0 && espRebootPending && resets==0);
  service(restartEsp); // initialize grace period after the HP ACK
  assert(resets==0);
  fakeMillis+=kEspRebootGraceMs-1;
  service(restartEsp);
  assert(resets==0);
  ++fakeMillis;
  service(restartEsp);
  assert(resets==1 && !espRebootPending);
  fakeMillis+=10000;
  for(unsigned i=0;i<10;++i)service(restartEsp);
  assert(resets==1); // exactly one hardware pulse

  // Interrupted HP BASIC statement (e.g. STOP, mailbox timeout) aborts.
  prepare();
  onOpcode(8);
  service(restartEsp);
  assert(state==2);
  onOpcode(0);
  fakeMillis+=10000;
  service(restartEsp);
  assert(state==0 && resets==0 && !espRebootPending);

  // A second command during the reboot grace period is rejected locally
  // and cannot schedule a second reset.
  prepare();
  onOpcode(8);service(restartEsp);
  while(onReply()){}
  assert(espRebootPending);
  onOpcode(8);service(restartEsp);
  assert(state==3);
  while(onReply()){}
  assert(espRebootPending);
  service(restartEsp);
  fakeMillis+=kEspRebootGraceMs;
  service(restartEsp);
  assert(resets==1);
  std::puts("PASS: CPREBOOT local ESP EN pulse after HP ACK; timeout, duplicate and no-UART safety");
}
