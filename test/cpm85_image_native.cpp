#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <string>
#include <limits.h>

struct FakeSlot { template<class T> FakeSlot &operator=(const T&) {return *this;} };
struct JsonObject { FakeSlot operator[](const char*) {return {}; } };
#include "EBTKS_CPM85_Image.h"

int main() {
  using namespace Cpm85Image;
  char clean[kNameMax] = {};
  assert(validName("WORK.DSK",clean) && std::strcmp(clean,"WORK.DSK")==0);
  assert(validName("WORK",clean) && std::strcmp(clean,"WORK.DSK")==0);
  for (const char *bad : {"", "../PASSWD.DSK", ".HIDDEN.DSK", "A/B.DSK",
                           "A\\B.DSK", "T.TXT", "A..DSK", "AB*.DSK"}) {
    assert(!validName(bad,clean));
  }
  begin("WORK");
  assert(state==RUNNING && SD.exists("/cpm/disks/WORK.DSK"));
  service();
  assert(written==4096);
  assert(state==RUNNING);
  unsigned loops=1;
  while(state==RUNNING && loops++<3000)service();
  assert(state==DONE && written==kBytes && loops>1000);
  const auto &bytes=SD.entries.at("/cpm/disks/WORK.DSK")->bytes;
  assert(bytes.size()==kBytes);
  assert(std::all_of(bytes.begin(),bytes.end(),[](uint8_t c){return c==0xE5;}));

  begin("WORK.DSK");
  assert(state==ERROR && std::strcmp(error,"file_exists")==0);
  assert(SD.entries.at("/cpm/disks/WORK.DSK")->bytes.size()==kBytes);

  SD.failAt=SD.writtenTotal+8192;
  begin("FAIL.DSK");
  assert(state==RUNNING);
  while(state==RUNNING)service();
  assert(state==ERROR && std::strcmp(error,"sd_write_failed")==0);
  assert(!SD.exists("/cpm/disks/FAIL.DSK"));
  assert(SD.exists("/cpm/disks/WORK.DSK"));

  begin("../NO.DSK");
  assert(state==ERROR && !SD.exists("/cpm/disks/../NO.DSK"));
  std::puts("PASS: CPDMK native name safety, 8 MiB E5 fill, bounded writes, no overwrite and cleanup");
}
