#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// CPM85 disk-control mailbox. HP I/O bus ISR may only touch bounded RAM.
// Protocol: 0343 W ASCII argument bytes, 0344 W opcode (1=status,
// 2=mount, 3=unmount, 4=Z80 reset), 0345 R state, 0346 R reply bytes.
// State: 0=idle, 1=busy, 2=success/reply, 3=error/reply. Read bytes
// through the first NUL to release the mailbox for another command.
// This mailbox is independent of 0341/0342 keyboard focus control.
namespace Cpm85Drive {
static constexpr size_t kArgsMax=92;
static constexpr size_t kReplyMax=768;
static volatile uint8_t state=0;
static volatile uint8_t operation=0;
static volatile uint16_t sequence=0;
static volatile uint16_t readIndex=0;
static volatile uint16_t replyLength=0;
static volatile uint8_t argumentLength=0;
static char arguments[kArgsMax]={};
static char response[kReplyMax]={};
static uint16_t nextSequence=0;
static uint32_t lastOfferMs=0;

// These four functions are HP bus ISR callbacks: no UART, SD, JSON,
// logging, heap allocation or waits are allowed in them.
inline void onArgument(uint8_t value) {
  if(state!=0)return;
  if(value==0)return; // Command write supplies the terminator.
  if(value<32 || value>126 || argumentLength>=kArgsMax-1)return;
  arguments[argumentLength++]=(char)value;
}
inline void onOpcode(uint8_t code) {
  // 0 is an explicit HP-side timeout recovery/abort. A late ESP
  // acknowledgement is then ignored because the mailbox is no longer busy.
  if(code==0) {
    state=0; operation=0; argumentLength=0; readIndex=replyLength=0;
    lastOfferMs=0;
    return;
  }
  if(state!=0)return;
  if(code<1 || code>4){argumentLength=0;return;}
  arguments[argumentLength]=0;
  operation=code;
  sequence=++nextSequence;
  state=1;
  lastOfferMs=0;
}
inline uint8_t onState() { return state; }
inline uint8_t onReply() {
  if(state!=2 && state!=3)return 0;
  if(readIndex<replyLength)return (uint8_t)response[readIndex++];
  // Consume NUL and release. Reset argument length for the next request.
  state=0;
  operation=0;
  readIndex=replyLength=0;
  argumentLength=0;
  return 0;
}

// Called from Teensy's *ordinary loop* in its existing ESP UART poll.
inline void receiveAck(JsonVariantConst ack) {
  if(state!=1 || ack.isNull())return;
  const int id=ack["seq"] | -1;
  if(id<0 || id!=(int)sequence)return;
  const bool ok=ack["ok"] | false;
  const char *message=ack["text"] | (ok?"OK":"FAILED");
  size_t len=strnlen(message,kReplyMax-1);
  memcpy(response,message,len);
  response[len]=0;
  // Publish length and read cursor BEFORE marking reply available.
  noInterrupts();
  replyLength=(uint16_t)len;
  readIndex=0;
  state=ok?2:3;
  interrupts();
}
inline void offer(JsonObject out) {
  if(state!=1)return;
  const uint32_t now=millis();
  // Retry request if the Teensy->ESP response or subsequent ACK was lost.
  // ESP deduplicates by sequence so RESET is never executed twice.
  if(lastOfferMs && (uint32_t)(now-lastOfferMs)<150)return;
  lastOfferMs=now?now:1;
  JsonObject cmd=out["command"].to<JsonObject>();
  cmd["seq"]=(uint16_t)sequence;
  cmd["op"]=(uint8_t)operation;
  cmd["args"]=arguments;
}
} // namespace Cpm85Drive

inline void onWriteCpm85DriveArgument(uint8_t value) {Cpm85Drive::onArgument(value);}
inline void onWriteCpm85DriveOpcode(uint8_t value) {Cpm85Drive::onOpcode(value);}
// EBTKS ioReadFuncPtr_t is bool (*)(void), NOT a byte-return callback.
// Publish the byte through readData and return true so the HP bus ISR
// actually drives the data bus (including value 0) during the read cycle.
inline bool onReadCpm85DriveState() {
  readData = Cpm85Drive::onState();
  return true;
}
inline bool onReadCpm85DriveReply() {
  readData = Cpm85Drive::onReply();
  return true;
}
