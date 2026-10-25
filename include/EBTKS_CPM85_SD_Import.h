#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "EBTKS_CPM85_SD_Export.h"

// B2c source ONLY: read a previously published /cpm/transfers file.
// Never accepts a raw SD path, never writes or renames SD files, and uses
// its own descriptor independent of generic UART _currFile and CP/M disks.
namespace Cpm85SdImport {
static constexpr uint16_t kMaxRecords=63;
static constexpr uint16_t kRecordBytes=128;
enum State : uint8_t { IDLE, ACTIVE, DONE, ERROR };
static State state=IDLE;
static FsFile sourceFile;
static char sourceName[16]={};
static char sourcePath[88]={};
static char lastError[40]={};
static uint32_t token=0;
static uint16_t records=0;
static uint16_t served=0;
static uint16_t sourceCrc=0xFFFF;

inline void fail(const char *message) {
  if(sourceFile)sourceFile.close();
  snprintf(lastError,sizeof(lastError),"%s",message);
  state=ERROR;
}
inline bool begin(const char *source,const char *&reason) {
  if(state==ACTIVE) {reason="import_in_progress";return false;}
  if(Cpm85SdExport::state==Cpm85SdExport::ACTIVE) {
    reason="export_in_progress";return false;
  }
  char canonical[16]{},leaf[16]{};
  if(!Cpm85SdExport::normalise(source,canonical,leaf)) {
    reason="invalid_source_name";return false;
  }
  char path[sizeof(sourcePath)]{};
  if(snprintf(path,sizeof(path),"/cpm/transfers/%s",leaf)>=(int)sizeof(path)) {
    reason="invalid_source_path";return false;
  }
  FsFile candidate=SD.open(path,O_RDONLY);
  if(!candidate) {reason="source_not_found";return false;}
  const uint64_t length=candidate.size();
  if(length==0 || length>uint64_t(kMaxRecords)*kRecordBytes ||
     (length%kRecordBytes)!=0) {
    candidate.close();
    reason="source_must_be_1_to_63_records";return false;
  }
  uint8_t block[kRecordBytes];
  uint16_t checked=0xFFFF;
  bool ok=true;
  for(uint16_t i=0;i<length/kRecordBytes;++i) {
    if(candidate.read(block,sizeof(block))!=(int)sizeof(block)) {
      ok=false;break;
    }
    checked=Cpm85SdExport::crcAppend(checked,block,sizeof(block));
  }
  if(!ok || !candidate.seek(0)) {
    candidate.close();reason="source_read_or_seek_failed";return false;
  }
  sourceFile=candidate;
  snprintf(sourceName,sizeof(sourceName),"%s",canonical);
  snprintf(sourcePath,sizeof(sourcePath),"%s",path);
  records=uint16_t(length/kRecordBytes);
  served=0;
  sourceCrc=checked;
  lastError[0]=0;
  if(++token==0)++token;
  state=ACTIVE;
  reason="ok";
  return true;
}
inline bool readRecord(uint32_t session,uint16_t index,uint8_t *out,
                       uint16_t &recordCrc,const char *&reason) {
  if(state!=ACTIVE || session!=token) {reason="wrong_session";return false;}
  if(!out || index!=served || index>=records) {
    reason="out_of_order_record";return false;
  }
  if(!sourceFile.seek(uint32_t(index)*kRecordBytes) ||
     sourceFile.read(out,kRecordBytes)!=(int)kRecordBytes) {
    fail("sd_read_failed");reason=lastError;return false;
  }
  recordCrc=Cpm85SdExport::crcAppend(0xFFFF,out,kRecordBytes);
  ++served;
  reason="ok";
  return true;
}
inline bool finish(uint32_t session,const char *&reason) {
  if(state!=ACTIVE || session!=token) {reason="wrong_session";return false;}
  if(served!=records) {reason="incomplete_source";return false;}
  sourceFile.close();
  state=DONE;
  reason="ok";
  return true;
}
inline bool abort(uint32_t session,const char *&reason) {
  if(state!=ACTIVE || session!=token) {reason="wrong_session";return false;}
  fail("aborted");
  reason="ok";
  return true;
}
inline void status(JsonObject reply) {
  reply["state"]=state==ACTIVE?"active":state==DONE?"done":
                 state==ERROR?"error":"idle";
  reply["session"]=token;
  reply["records"]=records;
  reply["served"]=served;
  reply["bytes"]=uint32_t(records)*kRecordBytes;
  reply["crc16_ccitt_false"]=sourceCrc;
  reply["source"]=sourceName;
  if(state==ERROR)reply["error"]=lastError;
}
} // namespace Cpm85SdImport
