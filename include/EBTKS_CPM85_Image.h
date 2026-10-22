#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>

// Incremental 8 MiB native CP/M image creation.  The Teensy writes directly
// to its SD card; no image payload is sent over the ESP32 UART.  Each service
// call writes at most 4 KiB, keeping the ordinary HP-85 poll loop responsive.
// No existing file is ever opened for truncation or overwritten.
namespace Cpm85Image {
static constexpr uint32_t kBytes = 8UL * 1024UL * 1024UL;
static constexpr uint16_t kSector = 512;
static constexpr uint8_t kSectorsPerPoll = 8;
static constexpr size_t kNameMax = 68;
enum State : uint8_t { IDLE, RUNNING, DONE, ERROR };
static State state = IDLE;
static FsFile file;
static char filePath[96] = {};
static char fileName[kNameMax] = {};
static char error[36] = {};
static uint32_t written = 0;
static uint8_t blank[kSector] = {};

inline bool validName(const char *input, char (&out)[kNameMax]) {
  if (!input || !input[0]) return false;
  const size_t n = strnlen(input, kNameMax);
  if (n == 0 || n >= kNameMax || input[0] == '.' ||
      strstr(input, "..") || strchr(input, '/') || strchr(input, '\\')) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = (unsigned char)input[i];
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
  }
  const char *dot = strrchr(input, '.');
  if (dot && strcasecmp(dot, ".dsk") != 0) return false;
  if (!dot && n + 4 >= kNameMax) return false;
  strlcpy(out, input, kNameMax);
  if (!dot) strlcat(out, ".DSK", kNameMax);
  return true;
}

inline void fail(const char *reason) {
  if (file) file.close();
  // Only remove this image created by our exclusive-open begin operation.
  if (filePath[0]) SD.remove(filePath);
  strlcpy(error, reason, sizeof(error));
  state = ERROR;
}
inline void begin(const char *name) {
  if (state == RUNNING) {strlcpy(error, "creation_in_progress", sizeof(error)); return;}
  fileName[0] = 0; filePath[0] = 0; written = 0;
  char clean[kNameMax] = {};
  if (!validName(name, clean)) {state = ERROR; strlcpy(error, "invalid_filename", sizeof(error)); return;}
  char path[sizeof(filePath)] = {};
  const int n = snprintf(path, sizeof(path), "/cpm/disks/%s", clean);
  if (n <= 0 || n >= (int)sizeof(path)) {state = ERROR; strlcpy(error, "path_too_long", sizeof(error)); return;}
  // Check prior to O_EXCL as a second, independent safeguard.
  if (SD.exists(path)) {state = ERROR; strlcpy(error, "file_exists", sizeof(error)); return;}
  file = SD.open(path, O_RDWR | O_CREAT | O_EXCL);
  if (!file) {state = ERROR; strlcpy(error, "sd_open_failed", sizeof(error)); return;}
  strlcpy(fileName, clean, sizeof(fileName));
  strlcpy(filePath, path, sizeof(filePath));
  error[0] = 0;
  written = 0;
  memset(blank, 0xE5, sizeof(blank));  // CP/M unused-directory marker
  state = RUNNING;
}
inline void service() {
  if (state != RUNNING) return;
  for (uint8_t i = 0; i < kSectorsPerPoll && written < kBytes; ++i) {
    if (file.write(blank, sizeof(blank)) != sizeof(blank)) {
      fail("sd_write_failed"); return;
    }
    written += sizeof(blank);
  }
  if (written == kBytes) {
    if (!file.sync()) {fail("sd_flush_failed"); return;}
    file.close();
    // Size verification uses a new read-only descriptor.
    FsFile verify = SD.open(filePath, O_RDONLY);
    if (!verify) {fail("sd_verify_open_failed"); return;}
    const bool ok = verify.size() == kBytes;
    verify.close();
    if (!ok) {fail("sd_size_mismatch"); return;}
    state = DONE;
  }
}
inline void status(JsonObject reply) {
  reply["state"] = state == RUNNING ? "running" :
                   state == DONE ? "done" : state == ERROR ? "error" : "idle";
  reply["written"] = written;
  reply["bytes"] = kBytes;
  reply["name"] = fileName;
  if (state == ERROR) reply["error"] = error;
}
} // namespace Cpm85Image
