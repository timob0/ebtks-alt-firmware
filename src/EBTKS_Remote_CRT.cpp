#include <Arduino.h>
#include <string.h>
#include "Base64.h"

// EBTKS_CRT.cpp owns `current_screen`. Its video_capt_t is an anonymous
// typedef local to that source file, so do not redeclare it here with a
// different C++ type. Instead bind a byte-view identifier to the existing
// linker symbol and read its object representation. Access through uint8_t
// (unsigned char on Teensy) is valid for inspecting an object's bytes.
extern uint8_t ebtksCurrentScreenBytes[] asm("current_screen");
extern bool Is8687;

static const size_t CRT_VRAM_OFFSET = 0;
static const size_t CRT_SAD_OFFSET  = 16384;

static const size_t HP85_ALPHA_BYTES = 2048;
static const size_t HP85_GRAPH_OFFSET = 2048;
static const size_t HP85_GRAPH_BYTES = 6144;       // 256 x 192 / 8
static const size_t HP85_GRAPH_CHUNK_BYTES = 512;
static const uint8_t HP85_GRAPH_CHUNK_COUNT = HP85_GRAPH_BYTES / HP85_GRAPH_CHUNK_BYTES;

static int encodeVisibleHp85Alpha(char *buff, size_t capacity)
{
  if (!buff || capacity == 0)
    return -1;

  buff[0] = '\0';

  if (Is8687)
  {
    Serial.printf("Remote CRT alpha display is not yet supported on HP86/87\r\n");
    return -1;
  }

  uint16_t sad = 0;
  memcpy(&sad, ebtksCurrentScreenBytes + CRT_SAD_OFFSET, sizeof(sad));

  // HP-85 SAD is a nibble address. The mirror stores packed bytes, so divide
  // by two. Alpha RAM is 32 x 64 characters and wraps after byte 2047.
  static uint8_t visible[512];
  int addr = sad / 2;

  for (size_t i = 0; i < sizeof(visible); ++i)
  {
    visible[i] = ebtksCurrentScreenBytes[CRT_VRAM_OFFSET + addr++];
    if (addr >= (int)HP85_ALPHA_BYTES)
      addr = 0;
  }

  const int required = Base64.encodedLength(sizeof(visible));
  if (required < 0 || capacity <= (size_t)required)
  {
    Serial.printf("Remote CRT Base64 buffer too small: need %d + terminator, have %u\r\n",
                  required, (unsigned)capacity);
    return -1;
  }

  const int encoded = Base64.encode(buff, (char *)visible, sizeof(visible));
  if (encoded < 0)
  {
    buff[0] = '\0';
    Serial.printf("Remote CRT alpha Base64 encode failed\r\n");
    return -1;
  }

  return encoded;
}

// Return one self-identifying chunk of a frozen HP-85 graphics snapshot.
// The existing get_graph JSON command calls this repeatedly. Each decoded
// payload is:
//   byte 0 : snapshot generation
//   byte 1 : chunk index (0..11)
//   byte 2 : chunk count (12)
//   bytes 3..514 : 512 graphics bytes
//
// A fresh 6144-byte snapshot is captured immediately before chunk 0 is sent.
// The generation byte lets the browser discard a partial old cycle and wait
// for chunk 0, making captures robust even if an earlier transfer was aborted.
static int encodeHp85GraphChunk(char *buff, size_t capacity)
{
  if (!buff || capacity == 0)
    return -1;

  buff[0] = '\0';

  if (Is8687)
  {
    Serial.printf("Remote CRT graphics display is not yet supported on HP86/87\r\n");
    return -1;
  }

  static uint8_t snapshot[HP85_GRAPH_BYTES];
  static uint8_t frame[HP85_GRAPH_CHUNK_BYTES + 3];
  static uint8_t chunkIndex = 0;
  static uint8_t generation = 0;

  if (chunkIndex == 0)
  {
    memcpy(snapshot,
           ebtksCurrentScreenBytes + CRT_VRAM_OFFSET + HP85_GRAPH_OFFSET,
           sizeof(snapshot));
    generation++;
  }

  frame[0] = generation;
  frame[1] = chunkIndex;
  frame[2] = HP85_GRAPH_CHUNK_COUNT;
  memcpy(frame + 3,
         snapshot + ((size_t)chunkIndex * HP85_GRAPH_CHUNK_BYTES),
         HP85_GRAPH_CHUNK_BYTES);

  const int required = Base64.encodedLength(sizeof(frame));
  if (required < 0 || capacity <= (size_t)required)
  {
    Serial.printf("Remote CRT graphics Base64 buffer too small: need %d + terminator, have %u\r\n",
                  required, (unsigned)capacity);
    return -1;
  }

  const int encoded = Base64.encode(buff, (char *)frame, sizeof(frame));
  if (encoded < 0)
  {
    buff[0] = '\0';
    Serial.printf("Remote CRT graphics Base64 encode failed\r\n");
    return -1;
  }

  chunkIndex++;
  if (chunkIndex >= HP85_GRAPH_CHUNK_COUNT)
    chunkIndex = 0;

  return encoded;
}

// Live JSON CRT hook used by EBTKS_ESP.h. The legacy function in
// EBTKS_CRT.cpp remains for historical reference, but the active web path
// lives here as ordinary compiled source rather than build middleware.
void dumpCrtRemoteAsBase64(char *buff, bool comp_graph)
{
  // EBTKS_ESP.h supplies its MAX_STR_LEN+2 buffer (4098 bytes).
  static const size_t OUTPUT_CAPACITY = 4098;

  if (!buff)
    return;

  buff[0] = '\0';

  if (comp_graph)
  {
    encodeHp85GraphChunk(buff, OUTPUT_CAPACITY);
    return;
  }

  encodeVisibleHp85Alpha(buff, OUTPUT_CAPACITY);
}
