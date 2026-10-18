#pragma once
// Experimental Media Manager 8 MiB image upload: ESP32 SPI2 slave TX (MISO)
// -> Teensy 4.1 SPI0 master RX. NEVER install a READY GPIO interrupt:
// EBTKS HP-85 IRQ_GPIO6789 is exclusively owned by the custom HP bus ISR.
// Only operates on a unique staging SD file; production CP/M writes untouched.
#include <Arduino.h>
#include <SPI.h>
#include <strings.h>
#include "EBTKS_SPI_Stream.h"

struct EbtksImageStage {
  FsFile file;
  char target[112] = {};
  char stage[112] = {};
  uint32_t expected = 0, received = 0, crc = 0xFFFFFFFFUL;
  uint16_t session = 0;
  bool active = false, failed = false;
};
static EbtksImageStage ebtksImageStage;
static constexpr uint32_t EBTKS_IMAGE_NATIVE_BYTES = 8388608UL;

inline uint32_t ebtksImageCrc32(uint32_t crc, const uint8_t *p, size_t n) {
  for(size_t i=0;i<n;++i) {
    crc ^= p[i];
    for(int b=0;b<8;++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0UL);
  }
  return crc;
}
inline bool ebtksImageNameOk(const char *name) {
  if (!name) return false;
  const size_t n=strlen(name);
  if(n<5 || n>55 || strcasecmp(name+n-4,".dsk")) return false;
  for(size_t i=0;i<n-4;++i) {
    char c=name[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
         (c>='0'&&c<='9')||c=='_'||c=='-')) return false;
  }
  return true;
}
inline void ebtksImageReset(bool removeStaging) {
  if(ebtksImageStage.file) ebtksImageStage.file.close();
  if(removeStaging && ebtksImageStage.stage[0]) SD.remove(ebtksImageStage.stage);
  ebtksImageStage.active=false;
  ebtksImageStage.failed=false;
  ebtksImageStage.session=0;
  ebtksImageStage.stage[0]=0;
  ebtksImageStage.target[0]=0;
  ebtksImageStage.expected=ebtksImageStage.received=0;
  ebtksImageStage.crc=0xFFFFFFFFUL;
}
inline bool ebtksImageBegin(const char *name, uint32_t bytes,
                            uint16_t session, const char *&reason) {
  reason="bad_name_or_size";
  if(!ebtksImageNameOk(name) || bytes!=EBTKS_IMAGE_NATIVE_BYTES || !session)
    return false;
  // Only one image writer per Teensy. Reset and delete a previous unfinished
  // file when beginning a NEW session (explicit restart).
  if(ebtksImageStage.active) { reason="session_busy"; return false; }
  ebtksImageReset(false);
  snprintf(ebtksImageStage.target,sizeof(ebtksImageStage.target),
           "/cpm/disks/%s",name);
  snprintf(ebtksImageStage.stage,sizeof(ebtksImageStage.stage),
           "/cpm/disks/.up%04X.tmp",(unsigned)session);
  reason="target_exists";
  if(SD.exists(ebtksImageStage.target)) { ebtksImageReset(false); return false; }
  reason="staging_exists";
  if(SD.exists(ebtksImageStage.stage)) { ebtksImageReset(false); return false; }
  reason="sd_create_failed";
  ebtksImageStage.file=SD.open(ebtksImageStage.stage,O_WRONLY|O_CREAT|O_EXCL);
  if(!ebtksImageStage.file) { ebtksImageReset(false); return false; }
  ebtksImageStage.session=session;
  ebtksImageStage.expected=bytes;
  ebtksImageStage.received=0;
  ebtksImageStage.crc=0xFFFFFFFFUL;
  ebtksImageStage.active=true;
  reason="ready";
  return true;
}
// Poll the same READY line and use the same 4 MHz MODE0 signal timing as the
// proven Teensy->ESP32 sender. We clock dummy MOSI and receive ESP32 MISO.
inline bool ebtksImageReceiveFrame(uint16_t session,uint16_t block,
                                   bool moreFrames,uint16_t &aggregate,
                                   const char *&reason) {
  uint32_t start=micros();
  while(!digitalReadFast(EBTKS_SPI_TEST_READY)) {
    if((uint32_t)(micros()-start)>2000000UL) {
      reason="ready_high_timeout"; return false;
    }
  }
  alignas(4) uint8_t frame[EbtksStream::FRAME_BYTES];
  SPI.beginTransaction(SPISettings(4000000UL,MSBFIRST,SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS,LOW);
  delayMicroseconds(3);
  for(size_t i=0;i<EbtksStream::FRAME_BYTES;++i) frame[i]=SPI.transfer(0);
  digitalWriteFast(EBTKS_SPI_TEST_CS,HIGH);
  SPI.endTransaction();
  // Observe READY LOW immediately, before SD writes or any other operation:
  // required to avoid mistaking the previous DMA descriptor for the next.
  if(moreFrames) {
    start=micros();
    while(digitalReadFast(EBTKS_SPI_TEST_READY)) {
      if((uint32_t)(micros()-start)>2000000UL) {
        reason="ready_low_timeout"; return false;
      }
    }
  }
  const EbtksStream::Check check=EbtksStream::check(
      frame,EbtksStream::KIND_IMAGE_UPLOAD,session,block);
  if(check!=EbtksStream::Check::OK) {
    reason=EbtksStream::checkName(check);
    ebtksImageStage.failed=true;
    return true; // frame consumed, finish batch so ESP32 DMA is fully drained
  }
  const uint8_t *payload=frame+EbtksStream::HEADER_BYTES;
  if(!ebtksImageStage.failed && ebtksImageStage.file &&
     ebtksImageStage.received+512<=ebtksImageStage.expected &&
     ebtksImageStage.file.write(payload,512)==512) {
    ebtksImageStage.crc=ebtksImageCrc32(ebtksImageStage.crc,payload,512);
    ebtksImageStage.received+=512;
    aggregate=EbtksStream::crc16(payload,512,aggregate);
    return true;
  }
  reason="sd_write_failed";
  ebtksImageStage.failed=true;
  return true;
}
inline bool ebtksImageWrite(uint16_t session,uint32_t start,uint16_t count,
                            uint16_t &aggregate,uint16_t &frames,
                            const char *&reason) {
  aggregate=0xFFFF; frames=0; reason="invalid_session_or_range";
  if(!ebtksImageStage.active||ebtksImageStage.failed ||
     session!=ebtksImageStage.session||count==0||count>16 ||
     start!=ebtksImageStage.received/512 ||
     (uint64_t)(start+count)*512>ebtksImageStage.expected) return false;
  ebtksSpiTestInit();
  for(uint16_t i=0;i<count;++i) {
    const char *frameReason="ok";
    bool got=ebtksImageReceiveFrame(session,i,i+1<count,aggregate,frameReason);
    if(!got) {reason=frameReason;ebtksImageStage.failed=true;break;}
    ++frames;
    if(strcmp(frameReason,"ok")) reason=frameReason;
  }
  if(!ebtksImageStage.failed) reason="ok";
  return frames==count && !ebtksImageStage.failed;
}
inline bool ebtksImageFinish(uint16_t session,uint32_t expectedCrc,
                             uint32_t &readbackCrc,const char *&reason) {
  readbackCrc=0;
  reason="incomplete_or_bad_session";
  if(!ebtksImageStage.active||ebtksImageStage.failed||
      ebtksImageStage.session!=session||
      ebtksImageStage.received!=ebtksImageStage.expected) return false;
  reason="sync_failed";
  if(!ebtksImageStage.file.sync()) return false;
  ebtksImageStage.file.close();
  if((ebtksImageStage.crc ^ 0xFFFFFFFFUL)!=expectedCrc) {
    reason="stream_crc32_mismatch";return false;
  }
  FsFile verify=SD.open(ebtksImageStage.stage,O_RDONLY);
  reason="readback_failed";
  if(!verify) return false;
  uint8_t buf[512];
  uint32_t total=0,crc=0xFFFFFFFFUL;
  while(total<ebtksImageStage.expected) {
    if(verify.read(buf,sizeof(buf))!=(int)sizeof(buf)) break;
    crc=ebtksImageCrc32(crc,buf,sizeof(buf));
    total+=sizeof(buf);
  }
  verify.close();
  readbackCrc=crc^0xFFFFFFFFUL;
  if(total!=ebtksImageStage.expected || readbackCrc!=expectedCrc) {
    reason="sd_readback_crc32_mismatch";return false;
  }
  reason="target_exists";
  if(SD.exists(ebtksImageStage.target)) return false;
  reason="publish_failed";
  if(!SD.rename(ebtksImageStage.stage,ebtksImageStage.target))return false;
  ebtksImageReset(false);
  reason="ok";
  return true;
}

inline bool ebtksImageRamProbe(uint16_t session,uint16_t &crc,
                               const char *&reason) {
  crc=0xFFFF;reason="ready_timeout";
  ebtksSpiTestInit();
  const uint32_t started=micros();
  while(!digitalReadFast(EBTKS_SPI_TEST_READY)) {
    if((uint32_t)(micros()-started)>2000000UL)return false;
  }
  alignas(4) uint8_t frame[EbtksStream::FRAME_BYTES]={};
  SPI.beginTransaction(SPISettings(4000000UL,MSBFIRST,SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS,LOW);
  delayMicroseconds(3);
  for(size_t i=0;i<EbtksStream::FRAME_BYTES;++i)frame[i]=SPI.transfer(0);
  digitalWriteFast(EBTKS_SPI_TEST_CS,HIGH);
  SPI.endTransaction();
  auto check=EbtksStream::check(frame,EbtksStream::KIND_IMAGE_UPLOAD,
                                session,0);
  if(check!=EbtksStream::Check::OK) {
    reason=EbtksStream::checkName(check);return false;
  }
  const uint8_t *payload=frame+EbtksStream::HEADER_BYTES;
  crc=EbtksStream::crc16(payload,512);
  for(size_t i=0;i<512;++i) {
    if(payload[i]!=(uint8_t)((i*29U+session*7U+0x53U)&255U)) {
      reason="payload_pattern_mismatch";return false;
    }
  }
  reason="ok";
  return true;
}
