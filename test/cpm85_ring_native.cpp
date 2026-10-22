#include <cassert>
#include <cstdio>
#include "EBTKS_CPM85_Keyboard.h"

int main(){
  using namespace Cpm85Keys;
  assert(!isAttached());
  onWriteCpm85Key('A');
  assert(pop()==-1); // no key admission before attach
  onWriteCpm85Control(1);
  assert(isAttached());
  onWriteCpm85Key('B');
  onWriteCpm85Key(0232);
  onWriteCpm85Key(0);
  assert(pop()=='B');
  assert(pop()==0232);
  assert(pop()==0);
  assert(pop()==-1);
  onWriteCpm85Control(19); // unknown control should not change state
  assert(isAttached());
  for(int n=0;n<127;++n)onWriteCpm85Key((uint8_t)n);
  assert(pending()==127);
  onWriteCpm85Key(222);
  assert(overflowCount()==1);
  for(int n=0;n<127;++n)assert(pop()==n);
  assert(pop()==-1);
  onWriteCpm85Key('C');
  onWriteCpm85Control(0);
  assert(!isAttached());
  // K8 detach intentionally retains the bytes already captured for CP/M.
  assert(pop()=='C');
  assert(pop()==-1);
  onWriteCpm85Key('D');
  assert(pop()==-1);
  onWriteCpm85Control(1);
  assert(isAttached() && overflowCount()==0 && pop()==-1);
  std::puts("PASS: CPM85 EBTKS FIFO, attach/detach, overflow and raw HP bytes");
}
