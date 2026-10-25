#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <string>
struct FakeSlot {template<class T> FakeSlot &operator=(const T&) {return *this;}};
struct JsonObject {FakeSlot operator[](const char*) {return {};}};
#include "EBTKS_CPM85_SD_Export.h"

int main() {
  using namespace Cpm85SdExport;
  const char *reason=nullptr;
  char canonical[16]{},leaf[16]{};
  assert(normalise("a:stat.com",canonical,leaf));
  assert(std::strcmp(canonical,"A:STAT.COM")==0);
  assert(std::strcmp(leaf,"A_STAT.COM")==0);
  for(const char *bad: {"", "A:", "Q:HI.COM", "A:../HI", "A:HI/THERE",
                        "A:HI\\THERE", "A:LONGNAME9.COM", "A:X.EXTX",
                        "A:.BAD", "A:HI..COM", "A:HI*", "A:HI "}) {
    assert(!normalise(bad,canonical,leaf));
  }
  const uint8_t digits[]={'1','2','3','4','5','6','7','8','9'};
  assert(crcAppend(0xFFFF,digits,sizeof(digits))==0x29B1);

  uint8_t first[128],second[128];
  for(unsigned i=0;i<128;++i) {
    first[i]=(uint8_t)i;
    second[i]=(uint8_t)(255-i);
  }
  const uint16_t a=crcAppend(0xFFFF,first,128);
  const uint16_t b=crcAppend(0xFFFF,second,128);
  const uint16_t both=crcAppend(a,second,128);
  assert(begin("a:stat.com",reason));
  const uint32_t token=session;
  assert(state==ACTIVE && SD.exists("/cpm/transfers/.A_STAT.COM.part"));
  assert(!SD.exists("/cpm/transfers/A_STAT.COM"));
  assert(!begin("A:OTHER.COM",reason)); // in-flight transfer owns staging
  assert(std::strcmp(reason,"transfer_in_progress")==0);
  assert(!append(token+1,0,first,128,a,reason));
  assert(state==ACTIVE);
  assert(append(token,0,first,128,a,reason));
  assert(append(token,1,second,128,b,reason));
  assert(records==2 && crc==both);
  assert(finish(token,2,both,reason));
  assert(state==DONE && SD.exists("/cpm/transfers/A_STAT.COM"));
  assert(!SD.exists("/cpm/transfers/.A_STAT.COM.part"));
  const auto &file=SD.entries.at("/cpm/transfers/A_STAT.COM")->bytes;
  assert(file.size()==256);
  assert(std::equal(first,first+128,file.begin()));
  assert(std::equal(second,second+128,file.begin()+128));
  assert(!begin("A:STAT.COM",reason));
  assert(std::strcmp(reason,"destination_exists")==0);
  assert(file.size()==256);

  assert(begin("A:BROKEN.BIN",reason));
  const uint32_t broken=session;
  assert(!append(broken,0,first,128,uint16_t(a^1),reason));
  assert(state==ERROR && !SD.exists("/cpm/transfers/.A_BROKEN.BIN.part"));
  assert(!SD.exists("/cpm/transfers/A_BROKEN.BIN"));

  assert(begin("A:ORDER.BIN",reason));
  const uint32_t order=session;
  assert(!append(order,1,first,128,a,reason));
  assert(state==ERROR && !SD.exists("/cpm/transfers/.A_ORDER.BIN.part"));

  assert(begin("A:VERIFY.BIN",reason));
  const uint32_t verify=session;
  assert(append(verify,0,first,128,a,reason));
  SD.entries.at("/cpm/transfers/.A_VERIFY.BIN.part")->bytes[5] ^= 0x55;
  assert(!finish(verify,1,a,reason));
  assert(state==ERROR && !SD.exists("/cpm/transfers/A_VERIFY.BIN"));
  assert(!SD.exists("/cpm/transfers/.A_VERIFY.BIN.part"));

  assert(begin("A:FAILED.BIN",reason));
  const uint32_t fail=session;
  SD.failAt=SD.writtenTotal+64;
  assert(!append(fail,0,first,128,a,reason));
  assert(!SD.exists("/cpm/transfers/.A_FAILED.BIN.part"));
  SD.failAt=SIZE_MAX;

  assert(begin("B:EMPTY.DAT",reason));
  assert(finish(session,0,0xFFFF,reason));
  assert(SD.entries.at("/cpm/transfers/B_EMPTY.DAT")->bytes.empty());

  assert(begin("A:ABORT.BIN",reason));
  const uint32_t cancelled=session;
  assert(append(cancelled,0,first,128,a,reason));
  assert(abort(cancelled,reason));
  assert(!SD.exists("/cpm/transfers/.A_ABORT.BIN.part"));
  assert(!SD.exists("/cpm/transfers/A_ABORT.BIN"));

  // A leftover temporary file from a power loss must not be reclaimed or
  // deleted behind the operator's back.
  SD.entries.emplace("/cpm/transfers/.A_STALE.BIN.part",
                     std::make_shared<MockImage>());
  assert(!begin("A:STALE.BIN",reason));
  assert(std::strcmp(reason,"stale_partial_exists")==0);
  assert(SD.exists("/cpm/transfers/.A_STALE.BIN.part"));

  std::puts("PASS: B2b Teensy staged export: CRC, readback, EOF, collision, cancellation and cleanup");
}
