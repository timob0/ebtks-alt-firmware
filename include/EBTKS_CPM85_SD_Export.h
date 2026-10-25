#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Stage B2b: isolated, bounded CP/M -> Teensy SD staging.
// This never opens a mounted .DSK file or uses the generic _currFile handle.
// Only an explicitly requested drive-qualified CP/M 8.3 name is accepted.
// No existing destination, including incomplete .part files, is overwritten.
namespace Cpm85SdExport {
static constexpr uint16_t kRecordBytes=128;
static constexpr uint16_t kMaxRecords=64;
enum State : uint8_t { IDLE, ACTIVE, DONE, ERROR };
static State state=IDLE;
static FsFile stagingFile;
static char source[16]={};
static char destination[88]={};
static char temporary[88]={};
static char lastError[40]={};
static bool ownsTemporary=false;
static uint32_t session=0;
static uint16_t records=0;
static uint16_t crc=0xFFFF;

inline uint16_t crcAppend(uint16_t seed,const uint8_t *data,size_t n) {
  for(size_t i=0;i<n;++i) {
    seed ^= uint16_t(data[i])<<8;
    for(unsigned b=0;b<8;++b)
      seed=(seed&0x8000)?uint16_t((seed<<1)^0x1021):uint16_t(seed<<1);
  }
  return seed;
}
inline bool normalise(const char *name,char (&out)[16],char (&leaf)[16]) {
  if(!name)return false;
  const size_t n=strnlen(name,16);
  if(n<3 || n>14 || name[1]!=':')return false;
  char drive=name[0];
  if(drive>='a' && drive<='z')drive-=32;
  if(drive<'A'||drive>'P')return false;
  unsigned pos=0,ext=0;
  bool dot=false;
  out[0]=drive;out[1]=':';
  leaf[0]=drive;leaf[1]='_';
  for(size_t i=2;i<n;++i) {
    char c=name[i];
    if(c>='a' && c<='z')c-=32;
    if(c=='.') {
      if(dot || pos==0 || i+1==n)return false;
      dot=true;
    } else {
      if(!((c>='A' && c<='Z') || (c>='0' && c<='9') ||
           c=='_' || c=='$' || c=='#' || c=='@' || c=='!' ||
           c=='%' || c=='&' || c==39 || c=='(' || c==')' ||
           c=='-' || c=='^'))return false;
      if((!dot && ++pos>8) || (dot && ++ext>3))return false;
    }
    out[i]=c;
    leaf[i]=c;
  }
  out[n]=0;leaf[n]=0;
  return true;
}
inline void failed(const char *reason) {
  if(stagingFile)stagingFile.close();
  if(ownsTemporary) {
    // Only remove the exclusive file made by this staging transaction.
    if(!SD.remove(temporary))reason="partial_cleanup_failed";
    ownsTemporary=false;
  }
  snprintf(lastError,sizeof(lastError),"%s",reason);
  state=ERROR;
}
inline bool begin(const char *name,const char *&reason) {
  if(state==ACTIVE) {reason="transfer_in_progress";return false;}
  char normal[16]={},leaf[16]={};
  if(!normalise(name,normal,leaf)) {reason="invalid_source_name";return false;}
  char dest[88]={},part[88]={};
  if(snprintf(dest,sizeof(dest),"/cpm/transfers/%s",leaf)>=(int)sizeof(dest) ||
     snprintf(part,sizeof(part),"/cpm/transfers/.%s.part",leaf)>=(int)sizeof(part)) {
    reason="path_too_long";return false;
  }
  if(!SD.exists("/cpm/transfers") && !SD.mkdir("/cpm/transfers")) {
    reason="transfer_dir_failed";return false;
  }
  if(SD.exists(dest)) {reason="destination_exists";return false;}
  if(SD.exists(part)) {reason="stale_partial_exists";return false;}
  stagingFile=SD.open(part,O_RDWR|O_CREAT|O_EXCL);
  if(!stagingFile) {reason="exclusive_create_failed";return false;}
  ownsTemporary=true;
  snprintf(source,sizeof(source),"%s",normal);
  snprintf(destination,sizeof(destination),"%s",dest);
  snprintf(temporary,sizeof(temporary),"%s",part);
  records=0;crc=0xFFFF;lastError[0]=0;
  if(++session==0)++session;
  state=ACTIVE;
  reason="ok";
  return true;
}
inline bool append(uint32_t token,uint16_t index,const uint8_t *data,
                   size_t len,uint16_t claimedCrc,const char *&reason) {
  if(state!=ACTIVE || token!=session) {reason="wrong_session";return false;}
  if(!data || len!=kRecordBytes || index!=records || records>=kMaxRecords) {
    failed("out_of_order_or_length");reason=lastError;return false;
  }
  if(crcAppend(0xFFFF,data,len)!=claimedCrc) {
    failed("record_crc_mismatch");reason=lastError;return false;
  }
  if(stagingFile.write(data,len)!=len) {
    failed("sd_write_failed");reason=lastError;return false;
  }
  crc=crcAppend(crc,data,len);
  ++records;
  reason="ok";
  return true;
}
inline bool finish(uint32_t token,uint16_t expectedRecords,
                   uint16_t expectedCrc,const char *&reason) {
  if(state!=ACTIVE || token!=session) {reason="wrong_session";return false;}
  if(expectedRecords!=records || expectedCrc!=crc) {
    failed("transfer_count_or_crc_mismatch");reason=lastError;return false;
  }
  if(!stagingFile.sync()) {failed("sd_sync_failed");reason=lastError;return false;}
  stagingFile.close();
  FsFile verify=SD.open(temporary,O_RDONLY);
  if(!verify) {failed("verify_open_failed");reason=lastError;return false;}
  bool verified=verify.size()==uint32_t(records)*kRecordBytes;
  uint16_t readCrc=0xFFFF;
  uint8_t block[kRecordBytes];
  for(uint16_t i=0;verified && i<records;++i) {
    if(verify.read(block,sizeof(block))!=(int)sizeof(block))verified=false;
    else readCrc=crcAppend(readCrc,block,sizeof(block));
  }
  verify.close();
  if(!verified || readCrc!=crc) {
    failed("sd_readback_mismatch");reason=lastError;return false;
  }
  // Never rename onto an existing destination. Only this service writes
  // below /cpm/transfers during an active transaction.
  if(SD.exists(destination)) {
    failed("destination_exists");reason=lastError;return false;
  }
  if(!SD.rename(temporary,destination)) {
    failed("publish_failed");reason=lastError;return false;
  }
  ownsTemporary=false;
  state=DONE;
  reason="ok";
  return true;
}
inline bool abort(uint32_t token,const char *&reason) {
  if(state!=ACTIVE || token!=session) {reason="wrong_session";return false;}
  failed("aborted");
  reason=lastError;
  return strcmp(lastError,"partial_cleanup_failed")!=0;
}
inline void status(JsonObject reply) {
  reply["state"]=state==ACTIVE?"active":state==DONE?"done":
                  state==ERROR?"error":"idle";
  reply["session"]=session;
  reply["records"]=records;
  reply["bytes"]=uint32_t(records)*kRecordBytes;
  reply["crc16_ccitt_false"]=crc;
  reply["source"]=source;
  reply["path"]=destination;
  if(state==ERROR)reply["error"]=lastError;
}
} // namespace Cpm85SdExport
