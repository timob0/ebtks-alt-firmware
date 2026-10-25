#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct FakeSlot {template<class T> FakeSlot &operator=(const T&) {return *this;}};
struct JsonObject {FakeSlot operator[](const char *) {return {};}};
#include "EBTKS_CPM85_SD_Import.h"

int main() {
  using namespace Cpm85SdImport;
  const char *why=nullptr;
  assert(!begin("A:MISSING.COM",why));
  assert(std::strcmp(why,"source_not_found")==0);
  for(const char *bad: {"A:../SECRETS", "Q:BAD.COM","A:HI/THERE",
                        "A:FOO..COM","X","/cpm/disks/A.DSK"}) {
    assert(!begin(bad,why));
  }

  uint8_t data[256];
  for(unsigned i=0;i<sizeof(data);++i)data[i]=(uint8_t)(i^0x89);
  {
    FsFile f=SD.open("/cpm/transfers/A_STAT.COM",O_RDWR|O_CREAT|O_EXCL);
    assert((bool)f && f.write(data,sizeof(data))==sizeof(data));
    f.close();
  }
  const uint16_t wanted=Cpm85SdExport::crcAppend(0xFFFF,data,sizeof(data));
  assert(begin("a:stat.com",why));
  const uint32_t current=token;
  assert(state==ACTIVE && records==2 && served==0);
  assert(sourceCrc==wanted);
  assert(std::strcmp(sourceName,"A:STAT.COM")==0);
  assert(!begin("A:STAT.COM",why));
  assert(std::strcmp(why,"import_in_progress")==0);

  uint8_t block[128]{};
  uint16_t got=0;
  assert(!readRecord(current,1,block,got,why));
  assert(std::strcmp(why,"out_of_order_record")==0);
  assert(!readRecord(current+1,0,block,got,why));
  assert(state==ACTIVE && served==0);
  assert(readRecord(current,0,block,got,why));
  assert(std::memcmp(block,data,128)==0);
  assert(got==Cpm85SdExport::crcAppend(0xFFFF,data,128));
  assert(!finish(current,why)); // cannot finish before all records
  assert(readRecord(current,1,block,got,why));
  assert(std::memcmp(block,data+128,128)==0);
  assert(finish(current,why));
  assert(state==DONE);
  assert(!readRecord(current,1,block,got,why));
  assert(SD.exists("/cpm/transfers/A_STAT.COM")); // source immutable

  assert(begin("A:STAT.COM",why));
  assert(abort(token,why));
  assert(state==ERROR);
  assert(SD.exists("/cpm/transfers/A_STAT.COM"));

  // Strict record alignment preflight; no CP/M file may be created from it.
  {
    const uint8_t bad[3]={1,2,3};
    FsFile f=SD.open("/cpm/transfers/A_BAD.DAT",O_RDWR|O_CREAT|O_EXCL);
    assert((bool)f && f.write(bad,sizeof(bad))==sizeof(bad));
    f.close();
  }
  assert(!begin("A:BAD.DAT",why));
  assert(std::strcmp(why,"source_must_be_1_to_63_records")==0);

  // Source too large for this guarded first import (63 logical records).
  {
    uint8_t large[128]{};
    FsFile f=SD.open("/cpm/transfers/B_LARGE.DAT",O_RDWR|O_CREAT|O_EXCL);
    assert((bool)f);
    for(unsigned i=0;i<64;++i)assert(f.write(large,sizeof(large))==sizeof(large));
    f.close();
  }
  assert(!begin("B:LARGE.DAT",why));
  assert(std::strcmp(why,"source_must_be_1_to_63_records")==0);
  std::puts("PASS: B2c SD source preflight, CRC, 128-byte order, bounded range, abort");
}
