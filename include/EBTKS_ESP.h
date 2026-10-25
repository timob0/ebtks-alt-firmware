/*

    Object to manage communications between the teensy and the esp32 of the EBTKS board
    Comms is via a uart to uart serial link

    See here on non IDE Firmware downloading for ESP32
      https://community.platformio.org/t/export-of-binary-firmware-files-for-esp32-download-tool/9253
      https://github.com/espressif/esptool
      D:\2021\HP-85_EBTKS_V1.0\ESP32_Binaries\Where_stuff_comes_from.txt

*/
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include "ArduinoJson.h"
#include "Base64.h"
#include "DirLine.h"
#include "EBTKS_SPI_Test.h"
#include "EBTKS_SPI_Stream.h"
#include "EBTKS_SPI_Image.h"
#include "EBTKS_CPM85_Keyboard.h" // ISR FIFO for HP-85 physical key events
#include "EBTKS_CPM85_Drive.h" // separate bounded management mailbox
#include "EBTKS_CPM85_Image.h" // incremental Teensy-local 8 MiB disk creation
#include "EBTKS_CPM85_SD_Export.h" // bounded, exclusive CP/M -> SD staged export

Base64Class ourBase64;
DirLine dl;
struct s_Dir_Entry dirent;

// 512 raw disk bytes become 684 Base64 characters. 1024 leaves sufficient
// room for the JSON envelope while matching the enlarged Serial2 RX buffer.
#define MAX_LINE 1024
#define MAX_READ_LEN 512
#define MAX_STR_LEN 4096
#define DISK_SECTOR_LEN 512


extern void dumpCrtAlphaAsBase64(char *buff, bool comp_graph);

//
//  simple circular buffer class
//
class CircBuff
{
  public:

  CircBuff()
  {
    _head = 0;
    _tail = 0;
  }
  //return -1 if nothing to read, else 0..255 if the char

  int getCh()
  {
    if (_head == _tail)
      {
        return -1;
      }
    else
      {
        int a = _buff[_tail++];

        if (_tail >= _len)
          {
            _tail = 0;
          }
          return a;
      }
  }

  void putch(uint8_t ch)
  {
      _buff[_head++] = ch;
      if (_head >= _len)
        {
          _head = 0;
        }
  }

  private:
  uint32_t _head;
  uint32_t _tail;
  const uint32_t _len = 100;
  uint8_t _buff[100];
};

//
//  Stream class that computes a crc on the stream
//
class CrcStream : public Stream
{
public:
  int available(void)
  {
    return 1;
  }

  int peek(void)
  {
    return 0;
  }

  void flush(void)
  {
  }

  void clearCrc()
  {
    _crc = 0xffffU; //prime the crc with the residual
  }

  //
  //  compute the crc from the write data
  //
  size_t write(uint8_t c)
  {
    uint8_t res = (uint16_t)c ^ _crc;
    _crc >>= 8;
    _crc ^= _crc_table[res];

    return 0;
  }

  uint16_t getCrc()
  {
    return _crc;
  }
  int read(void)
  {
    return 0;
  }

  using Print::write;
  size_t write(unsigned long n) { return write((uint8_t)n); }
  size_t write(long n) { return write((uint8_t)n); }
  size_t write(unsigned int n) { return write((uint8_t)n); }
  size_t write(int n) { return write((uint8_t)n); }

private:
  uint16_t _crc;

  const uint16_t _crc_table[256] = {
      0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
      0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
      0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
      0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
      0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
      0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
      0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
      0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
      0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
      0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
      0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
      0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
      0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
      0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
      0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
      0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
      0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
      0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
      0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
      0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
      0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
      0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
      0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
      0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
      0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
      0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
      0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
      0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
      0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
      0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
      0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
      0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040};
};

CrcStream crcstream;

class ESPComms
{
public:
  //
  //  call regularly to service the serial port
  //
  void poll()
  {
    if (getLine() == true)
    {
      // Serial.printf("RX:%s\n", _line);         //  floods the serial terminal
      processMsg();
    }
  }

  int getKybdCh()
  {
    return kbuff.getCh();
  }

  //
  //  attempt to construct a string of chars terminated by a CR
  //
  bool getLine()
  {
    static int gl_state = 0;
    bool found = false;

    while (Serial2.available() && (found == false))
    {
      char ch = Serial2.read();

      switch (gl_state)
      {
      case 0: //wait for beginning '{'

        if (ch == '{')
        {
          _lineNdx = 0;
          gl_state = 1;
          _line[_lineNdx++] = ch;
        }
        break;

      case 1: //accumulate a line until CR
        if (ch == 13)
        {
          _line[_lineNdx] = 0;
          gl_state = 0;
          found = true; //bail
        }
        else if (_lineNdx < (MAX_LINE - 1))
        {
          _line[_lineNdx++] = ch;
        }
        else
        {
          //we've overflowed
          _lineNdx = 0;
          gl_state = 0;
        }
        break;
      }
    }
    return found;
  }

  //
  //
  //
  void processMsg()
  {
    //message is in _line
    int len = strlen(_line);
    if (len < 5)
    {
      return;
    }
    //
    //  ArduinoJson 6
    //
    //StaticJsonDocument<3000> jdoc;
    //StaticJsonDocument<6000> jreply;

    //
    //  ArduinoJson 7 , JsonDocuments are elastic, so don't need size, are allocated on heap
    //
    JsonDocument jdoc;
    JsonDocument jreply;

    DeserializationError error = deserializeJson(jdoc, _line);
    if (error)
    {
      Serial.printf("Bad message from the ESP32. length:%d\r\n", len);
      return;
    }
    else
    {
      //  Serial.printf("message was properly formed\n");  //  Floods the serial terminal
    }

    // we were able to deserialise the JSON message

    if (jdoc["open"])
    {
      const char *filename = jdoc["open"]["path"] | "";
      int opts = jdoc["open"]["opts"] | O_RDONLY;

      _currFile = SD.open(filename, opts);

      //if opened, store the filehandle
      if (_currFile)
      {
        _currFd = 1;
      }

    //
    //  ArduinoJson 6
    //
    //JsonObject reply = jreply.createNestedObject("open");

    //
    //  ArduinoJson 7 , jreply.createNestedObject("open")  ->  jreply["open"].to<JsonObject>();
    //


      JsonObject reply = jreply["open"].to<JsonObject>();
      reply["fd"] = _currFd;
    }

    else if (jdoc["close"])
    {
      //process file close message
      int fd = jdoc["close"]["fd"] | -1;

      if ((_currFile) && (fd == _currFd))
      {
        _currFile.close();
        _currFd = -1;
        //_currFile = NULL; //invalidate the file handle
      }
      JsonObject reply = jreply["close"].to<JsonObject>();
      reply["fd"] = _currFd;
    }

    else if (jdoc["read"])
    {
      //process file read message
      int fd = jdoc["read"]["fd"] | -1;
      int len = jdoc["read"]["len"] | -1;

      //limit our max # of read bytes
      len = (len > MAX_READ_LEN) ? MAX_READ_LEN : len;
      int readLen = -1;
      if (_currFile && (fd == _currFd) && (len > 0))
      {
        readLen = _currFile.read(_buff, len);
        if (readLen > 0)
        {
          ourBase64.encode(_strBuff, (char *)_buff, readLen);
        }
      }
      // send response
      JsonObject reply = jreply["read"].to<JsonObject>();
      reply["fd"] = fd;
      reply["len"] = readLen;
      reply["data"] = _strBuff;
    }

    else if (jdoc["write"])
    {
      //process file write message. Decode first and only write if the decoded
      //length exactly matches the caller's requested length.
      int fd = jdoc["write"]["fd"] | -1;
      int requestedLen = jdoc["write"]["len"] | -1;
      requestedLen = (requestedLen > MAX_READ_LEN) ? MAX_READ_LEN : requestedLen;

      const char *encoded = jdoc["Write"]["data"] | "";
      int encodedLen = strlen(encoded);
      int decodedLen = ourBase64.decode((char *)_buff, (char *)encoded, encodedLen);

      int writeLen = -1;
      if (_currFile && (fd == _currFd) && (requestedLen > 0) &&
          (decodedLen == requestedLen))
      {
        writeLen = _currFile.write(_buff, decodedLen);
      }
      JsonObject reply = jreply["write"].to<JsonObject>();
      reply["fd"] = fd;
      reply["len"] = writeLen;
    }

    // B2b staged CP/M -> SD export. Restrict all paths inside the helper.
    // Separate FsFile from generic open/read/write and mounted .DSK handles.
    else if (jdoc["cpm_export"])
    {
      JsonVariantConst req=jdoc["cpm_export"].as<JsonVariantConst>();
      const char *op=req["op"] | "";
      const uint32_t token=req["session"] | (uint32_t)0;
      const char *reason="unknown_operation";
      bool ok=false;
      if (strcmp(op,"begin")==0) {
        const char *name=req["name"] | "";
        ok=Cpm85SdExport::begin(name,reason);
      } else if (strcmp(op,"write")==0) {
        const char *encoded=req["data"] | "";
        const size_t encodedLen=strnlen(encoded,180);
        const int rec=req["record"] | -1;
        const int claimed=req["crc16_ccitt_false"] | -1;
        if(encodedLen!=172 || rec<0 || rec>=64 ||
           claimed<0 || claimed>65535) {
          reason="invalid_record_envelope";
        } else {
          const int decoded=ourBase64.decode((char *)_buff,(char *)encoded,
                                             (int)encodedLen);
          ok=Cpm85SdExport::append(token,(uint16_t)rec,_buff,
                    decoded>0?(size_t)decoded:0,(uint16_t)claimed,reason);
        }
      } else if (strcmp(op,"finish")==0) {
        const int count=req["records"] | -1;
        const int claimed=req["crc16_ccitt_false"] | -1;
        if(count<0 || count>64 || claimed<0 || claimed>65535)
          reason="invalid_finish_envelope";
        else
          ok=Cpm85SdExport::finish(token,(uint16_t)count,
                                  (uint16_t)claimed,reason);
      } else if (strcmp(op,"status")==0) {
        ok=true;reason="ok";
      } else if (strcmp(op,"abort")==0) {
        ok=Cpm85SdExport::abort(token,reason);
      }
      JsonObject reply=jreply["cpm_export"].to<JsonObject>();
      reply["ok"]=ok;
      reply["reason"]=reason;
      Cpm85SdExport::status(reply);
    }

    // Single 528-byte reverse-SPI RAM test. Absolutely no SD access.
    else if (jdoc["spi_image_ram"])
    {
      int session=jdoc["spi_image_ram"]["session"] | 0;
      uint16_t crc=0;
      const char *reason="bad_session";
      bool ok=session>0 && session<=65535 &&
        ebtksImageRamProbe((uint16_t)session,crc,reason);
      JsonObject r=jreply["spi_image_ram"].to<JsonObject>();
      r["ok"]=ok;r["session"]=session;r["crc"]=crc;r["reason"]=reason;
      r["ready_protocol"]="poll_low_v2";
    }
    // Media Manager image transfer: protocol is opt-in and restricted to a
    // fresh, unmounted 8 MiB .dsk staging file. Normal CP/M writes unchanged.
    else if (jdoc["spi_image_begin"])
    {
      const char *name = jdoc["spi_image_begin"]["name"] | "";
      uint32_t bytes = jdoc["spi_image_begin"]["bytes"] | 0UL;
      int session = jdoc["spi_image_begin"]["session"] | 0;
      const char *reason = "invalid_session";
      bool ok = session>0 && session<=65535 &&
          ebtksImageBegin(name,bytes,(uint16_t)session,reason);
      JsonObject r=jreply["spi_image_begin"].to<JsonObject>();
      r["ok"]=ok; r["session"]=session; r["reason"]=reason;
      r["ready_protocol"]="poll_low_v2";
    }
    else if (jdoc["spi_image_write"])
    {
      int session=jdoc["spi_image_write"]["session"] | 0;
      uint32_t start=jdoc["spi_image_write"]["start"] | UINT32_MAX;
      int count=jdoc["spi_image_write"]["count"] | 0;
      uint16_t agg=0xFFFF,frames=0;
      const char *reason="bad_parameters";
      bool ok=false;
      if(session>0 && session<=65535 && count>0 && count<=16)
        ok=ebtksImageWrite((uint16_t)session,start,(uint16_t)count,
                           agg,frames,reason);
      JsonObject r=jreply["spi_image_write"].to<JsonObject>();
      r["ok"]=ok; r["session"]=session; r["start"]=start;
      r["count"]=count; r["frames"]=frames;
      r["aggregate_crc"]=agg;
      r["received"]=ebtksImageStage.received;
      r["reason"]=reason;
    }
    else if (jdoc["spi_image_finish"])
    {
      int session=jdoc["spi_image_finish"]["session"] | 0;
      uint32_t expected=jdoc["spi_image_finish"]["crc32"] | 0UL;
      uint32_t readback=0;
      const char *reason="bad_session";
      bool ok=session>0 && session<=65535 &&
        ebtksImageFinish((uint16_t)session,expected,readback,reason);
      JsonObject r=jreply["spi_image_finish"].to<JsonObject>();
      r["ok"]=ok; r["reason"]=reason; r["session"]=session;
      r["crc32"]=readback;
    }
    else if (jdoc["spi_image_abort"])
    {
      int session=jdoc["spi_image_abort"]["session"] | 0;
      // Session 0 is explicit operator recovery after an ESP32-only reboot.
      // It never publishes a partial image; only closes/removes staged data.
      bool ok=ebtksImageStage.active &&
              (session==ebtksImageStage.session || session==0);
      if(ok) ebtksImageReset(true);
      JsonObject r=jreply["spi_image_abort"].to<JsonObject>();
      r["ok"]=ok;
    }

    // Optional CN3 SPI diagnostic: ESP32 DMA is armed BEFORE this UART
    // request. With "bidi":true, Teensy also verifies its MISO receive bytes,
    // allowing one 512-byte transaction to validate both wire directions.
    // This never accesses SD or CP/M disk data.
    else if (jdoc["spi_probe"])
    {
      const int requestedLen = jdoc["spi_probe"]["len"] | 0;
      const uint32_t requestedHz = jdoc["spi_probe"]["hz"] | 0UL;
      const int requestedSeq = jdoc["spi_probe"]["seq"] | -1;
      const int requestedBlock = jdoc["spi_probe"]["block"] | 0;
      const int requestedPattern = jdoc["spi_probe"]["pattern"] | 0;
      const bool bidi = jdoc["spi_probe"]["bidi"] | false;
      const int requestedMode = jdoc["spi_probe"]["mode"] | 0;
      uint8_t misoSample16[16] = {};
      uint16_t mosiCrc = 0;
      uint16_t misoCrc = 0;
      int firstMisoMismatch = -1;
      uint32_t elapsedUs = 0;
      bool transferred = false;
      if (requestedLen == EBTKS_SPI_TEST_SIZE &&
          ebtksSpiTestValidHz(requestedHz) &&
          requestedSeq >= 0 && requestedSeq <= 255 &&
          requestedBlock >= 0 && requestedBlock <= 65535 &&
          requestedPattern >= 0 && requestedPattern <= 4 &&
          requestedMode >= 0 && requestedMode <= 1) {
        transferred = ebtksSpiTestRead(
            (uint8_t)requestedSeq, (uint16_t)requestedBlock,
            (uint8_t)requestedPattern, requestedHz, (uint8_t)requestedMode,
            bidi, mosiCrc, misoCrc, firstMisoMismatch, misoSample16, elapsedUs);
      }

      // Bounded raw samples are more diagnostic than a CRC alone. If
      // 512 bytes appear all zero, we can distinguish a stuck-low line
      // from bit shifting or a partially driven MISO output.
      char misoHead[33] = {};
      if (transferred && bidi) {
        static const char hexDigits[] = "0123456789ABCDEF";
        for (uint8_t i = 0; i < 16; ++i) {
          misoHead[i * 2] = hexDigits[misoSample16[i] >> 4];
          misoHead[i * 2 + 1] = hexDigits[misoSample16[i] & 15];
        }
      }
      JsonObject reply = jreply["spi_probe"].to<JsonObject>();
      reply["ok"] = transferred && firstMisoMismatch < 0;
      reply["bidi"] = bidi;
      reply["mode"] = requestedMode;
      reply["miso_head_hex"] = misoHead;
      reply["seq"] = requestedSeq;
      reply["block"] = requestedBlock;
      reply["pattern"] = requestedPattern;
      reply["hz"] = requestedHz;
      reply["len"] = transferred ? EBTKS_SPI_TEST_SIZE : 0;
      reply["crc"] = mosiCrc; // Teensy MOSI transmit CRC (legacy key)
      reply["miso_crc"] = misoCrc; // Teensy MISO receive CRC, if enabled
      reply["miso_first_mismatch"] = firstMisoMismatch; // -1 = all bytes equal
      reply["us"] = elapsedUs;
    }

    // Isolated GPIO-level CN3 MISO continuity diagnostic. This does NOT
    // transfer SPI data or touch SD. The ESP32 has first released SPI2 and
    // drives GPIO17 as plain output at the requested level. Temporarily
    // release Teensy SPI0's pin mux to sample physical T12 (pin 12), then
    // restore SPI0. This safely distinguishes a bad pin/wire from SPI DMA.
    else if (jdoc["spi_gpio_probe"])
    {
      const int requestedLevel = jdoc["spi_gpio_probe"]["level"] | -1;
      int observedLevel = -1;
      if (requestedLevel == 0 || requestedLevel == 1)
      {
        ebtksSpiTestInit(); // leave CS_N high, initialize SPI0 if needed
        SPI.end();
        pinMode(12, INPUT_PULLDOWN); // T12 / CN3-29
        delayMicroseconds(100);
        observedLevel = digitalReadFast(12) ? 1 : 0;
        SPI.begin(); // restore hardware SPI0 for subsequent normal tests
      }
      JsonObject reply = jreply["spi_gpio_probe"].to<JsonObject>();
      reply["ok"] = observedLevel == requestedLevel;
      reply["level"] = requestedLevel;
      reply["read"] = observedLevel;
    }

    // Batched generic SPI stream: control on UART, reusable binary frame on
    // MOSI. SD is only the first data producer; CRT may be added later.
    // UART PREPARE preflights the entire SD span so no DMA is armed on EOF.
    else if (jdoc["spi_stream_prepare"])
    {
      _spiStreamPrepared = false;
      const char *path = jdoc["spi_stream_prepare"]["path"] | "";
      const int session = jdoc["spi_stream_prepare"]["session"] | -1;
      const uint32_t first = jdoc["spi_stream_prepare"]["first"] | UINT32_MAX;
      const int count = jdoc["spi_stream_prepare"]["count"] | 0;
      const char *root = "/cpm/disks/";
      const size_t rootLen = strlen(root);
      const size_t pathLen = strlen(path);
      const bool nameOk = pathLen > rootLen && pathLen < sizeof(_spiStreamPath) &&
            !strncmp(path, root, rootLen) && !strstr(path + rootLen, "..") &&
            !strchr(path + rootLen, '/') && !strchr(path + rootLen, '\\');
      uint32_t fileBytes = 0;
      const char *reason = "invalid_request";
      if (nameOk && session >= 0 && session <= 65535 &&
          (count == 1 || count == 16 || count == 128) &&
          first <= (UINT32_MAX / DISK_SECTOR_LEN) - (uint32_t)count)
      {
        reason = "sd_open_failed";
        FsFile file = SD.open(path, O_RDONLY);
        if (file)
        {
          const uint64_t size = file.size();
          fileBytes = size > UINT32_MAX ? UINT32_MAX : (uint32_t)size;
          reason = "sd_sector_range";
          if (((uint64_t)first + (uint32_t)count) * DISK_SECTOR_LEN <= size)
          {
            strlcpy(_spiStreamPath, path, sizeof(_spiStreamPath));
            _spiStreamFirst = first;
            _spiStreamCount = (uint16_t)count;
            _spiStreamSession = (uint16_t)session;
            _spiStreamPrepared = true;
            reason = "ready";
          }
          file.close();
        }
      }
      JsonObject reply = jreply["spi_stream_prepare"].to<JsonObject>();
      // Prevent mismatched ESP32/Teensy revisions from starting a stream.
      // v2 deliberately uses NO Arduino READY interrupt (HP bus IRQ clash).
      reply["ready_protocol"] = "poll_low_v2";
      reply["ok"] = _spiStreamPrepared;
      reply["session"] = session;
      reply["first"] = first;
      reply["count"] = count;
      reply["file_size"] = fileBytes;
      reply["reason"] = reason;
    }
    else if (jdoc["spi_stream_send"])
    {
      const int session = jdoc["spi_stream_send"]["session"] | -1;
      const bool valid = _spiStreamPrepared && session >= 0 && session <= 65535 &&
                         _spiStreamSession == (uint16_t)session;
      // One-use session token, even on an invalid attempt.
      _spiStreamPrepared = false;
      uint16_t aggregate = 0xFFFF;
      uint32_t sdUs = 0, spiUs = 0, minUs = 0, maxUs = 0;
      uint16_t frames = 0, good = 0;
      int firstError = -1;
      const char *reason = valid ? "ok" : "unprepared_session";

      if (valid)
      {
        // Initialize diagnostic polling counters. Do not attach ANY
        // additional IRQ: IRQ_GPIO6789 is owned by the HP-85 bus ISR.
        ebtksSpiStreamBeginSession();
        const uint32_t opened = micros();
        FsFile file = SD.open(_spiStreamPath, O_RDONLY);
        bool ioOk = (bool)file;
        if (ioOk) ioOk = file.seek(_spiStreamFirst * (uint32_t)DISK_SECTOR_LEN);
        sdUs += (uint32_t)(micros() - opened);

        for (uint16_t block = 0; block < _spiStreamCount; ++block)
        {
          // Preserve one SPI frame per requested sector even on SD errors,
          // rather than leave an already-armed ESP DMA transaction stranded.
          uint16_t flags = 0;
          uint8_t *payload = _spiStreamFrame + EbtksStream::HEADER_BYTES;
          const uint32_t rdStart = micros();
          if (!ioOk || file.read(payload, DISK_SECTOR_LEN) != DISK_SECTOR_LEN)
          {
            ioOk = false;
            flags |= EbtksStream::FLAG_SOURCE_ERROR;
            memset(payload, 0, DISK_SECTOR_LEN);
            if (firstError < 0) {
              firstError = block;
              reason = "sd_read_failed";
            }
          }
          sdUs += (uint32_t)(micros() - rdStart);

          EbtksStream::stamp(_spiStreamFrame, EbtksStream::KIND_SD_READ,
                             _spiStreamSession, block, flags);
          uint32_t elapsed = 0;
          if (!ebtksSpiStreamSendFrame(_spiStreamFrame, elapsed,
                                       block + 1U < _spiStreamCount))
          {
            if (firstError < 0) firstError = block;
            reason = ebtksSpiStreamWaitFailure == 1 ?
                     "ready_low_timeout" :
                     ebtksSpiStreamWaitFailure == 2 ?
                     "ready_high_timeout" : "spi_ready_timeout";
            break;
          }
          ++frames;
          spiUs += elapsed;
          if (minUs == 0 || elapsed < minUs) minUs = elapsed;
          if (elapsed > maxUs) maxUs = elapsed;
          if (!flags) {
            ++good;
            aggregate = EbtksStream::crc16(payload, DISK_SECTOR_LEN, aggregate);
          }
        }
        if (file) file.close();
      }

      JsonObject reply = jreply["spi_stream_send"].to<JsonObject>();
      reply["ok"] = valid && frames == _spiStreamCount && good == _spiStreamCount;
      reply["session"] = session;
      reply["frames"] = frames;
      reply["good"] = good;
      reply["count"] = valid ? _spiStreamCount : 0;
      reply["aggregate_crc"] = aggregate;
      reply["sd_us"] = sdUs;
      reply["spi_us"] = spiUs;
      reply["min_us"] = minUs;
      reply["max_us"] = maxUs;
      reply["first_error"] = firstError;
      reply["ready_low_phases"] = ebtksSpiStreamReadyLowPhases;
      reply["ready_wait_us"] = ebtksSpiStreamReadyWaitUs;
      reply["ready_max_wait_us"] = ebtksSpiStreamMaxReadyWaitUs;
      reply["ready_wait_failure"] = (unsigned)ebtksSpiStreamWaitFailure;
      reply["reason"] = reason;
    }

    // Read-only diagnostic: first load ONE real SD sector into a dedicated
    // buffer, then request a separate SPI transfer after ESP32 DMA is armed.
    // This split prevents routine SD open/EOF errors from stranding DMA.
    // Neither phase changes the normal CP/M disk_read/disk_write protocol.
    else if (jdoc["spi_sd_prepare"])
    {
      _spiSdPrepared = false; // invalidate stale buffers on EVERY prepare
      const char *path = jdoc["spi_sd_prepare"]["path"] | "";
      const int sequence = jdoc["spi_sd_prepare"]["seq"] | -1;
      const uint32_t sector = jdoc["spi_sd_prepare"]["sector"] | 0UL;
      const uint32_t started = micros();
      const char *reason = "invalid_path_or_sequence";
      int readLen = -1;
      uint32_t fileBytes = 0;
      uint16_t crc = 0;

      // Only files directly under the established CP/M disk directory.
      // Reject traversal, nested directories and overlong paths.
      const char *root = "/cpm/disks/";
      const size_t rootLen = strlen(root);
      const size_t pathLen = strlen(path);
      if (sequence >= 0 && sequence <= 255 &&
          pathLen > rootLen && pathLen < 120 &&
          !strncmp(path, root, rootLen) &&
          !strstr(path + rootLen, "..") &&
          !strchr(path + rootLen, '/') &&
          !strchr(path + rootLen, '\\') &&
          sector <= (UINT32_MAX / DISK_SECTOR_LEN) - 1U)
      {
        reason = "sd_open_failed";
        FsFile sdSectorFile = SD.open(path, O_RDONLY);
        if (sdSectorFile)
        {
          const uint64_t size64 = sdSectorFile.size();
          fileBytes = size64 > UINT32_MAX ? UINT32_MAX : (uint32_t)size64;
          const uint32_t offset = sector * (uint32_t)DISK_SECTOR_LEN;
          reason = "sector_out_of_range";
          if ((uint64_t)offset + DISK_SECTOR_LEN <= size64)
          {
            reason = "sd_seek_failed";
            if (sdSectorFile.seek(offset))
            {
              reason = "sd_short_read";
              readLen = sdSectorFile.read(_spiSdSectorData, DISK_SECTOR_LEN);
              if (readLen == DISK_SECTOR_LEN)
              {
                crc = 0xFFFF;
                for (uint16_t i = 0; i < DISK_SECTOR_LEN; ++i)
                  crc = ebtksSpiTestCrcByte(crc, _spiSdSectorData[i]);
                _spiSdCrc = crc;
                _spiSdSector = sector;
                _spiSdSequence = (uint8_t)sequence;
                _spiSdPrepared = true;
                reason = "ready";
              }
            }
          }
          sdSectorFile.close();
        }
      }

      JsonObject reply = jreply["spi_sd_prepare"].to<JsonObject>();
      reply["ok"] = _spiSdPrepared;
      reply["seq"] = sequence;
      reply["sector"] = sector;
      reply["len"] = readLen;
      reply["crc"] = crc;
      reply["sd_us"] = (uint32_t)(micros() - started);
      reply["file_size"] = fileBytes;
      reply["reason"] = reason;
    }
    else if (jdoc["spi_sd_send"])
    {
      const int sequence = jdoc["spi_sd_send"]["seq"] | -1;
      const uint32_t sector = jdoc["spi_sd_send"]["sector"] | 0UL;
      const bool valid = _spiSdPrepared && sequence >= 0 && sequence <= 255 &&
                         _spiSdSequence == (uint8_t)sequence &&
                         _spiSdSector == sector;
      // Consume the preparation token before sending. No stale replay.
      _spiSdPrepared = false;
      uint16_t sentCrc = 0;
      uint32_t spiUs = 0;
      const bool transferred = valid &&
          ebtksSpiTestSendSdSector(_spiSdSectorData, sentCrc, spiUs);

      JsonObject reply = jreply["spi_sd_send"].to<JsonObject>();
      reply["ok"] = transferred && sentCrc == _spiSdCrc;
      reply["seq"] = sequence;
      reply["sector"] = sector;
      reply["len"] = transferred ? DISK_SECTOR_LEN : 0;
      reply["crc"] = sentCrc;
      reply["us"] = spiUs;
      reply["reason"] = !valid ? "no_matching_prepared_sector" :
                        !transferred ? "spi_ready_timeout" :
                        sentCrc != _spiSdCrc ? "transmit_crc_changed" : "sent";
    }

    // Fast path used by the Z80/CP-M virtual disks. One JSON request contains
    // one complete 512-byte sector, so open/seek/read-or-write/close happen
    // locally on the Teensy instead of requiring several UART round trips.
    // Newer ESP32 firmware may also provide an explicit image path.  Requests
    // without a path retain the historical /cpm/disks/<drive>.dsk behaviour.
    else if (jdoc["disk_read"])
    {
      int drive = jdoc["disk_read"]["drive"] | -1;
      uint32_t sector = jdoc["disk_read"]["sector"] | 0;
      const char *requestedPath = jdoc["disk_read"]["path"] | "";
      int readLen = -1;
      _strBuff[0] = 0;

      if (drive >= 0 && drive < 26)
      {
        char diskPath[128] = {};
        bool pathOk = false;
        if (requestedPath[0])
        {
          const char *root = "/cpm/disks/";
          size_t pathLen = strlen(requestedPath);
          if (!strncmp(requestedPath, root, strlen(root)) &&
              !strstr(requestedPath, "..") && pathLen < sizeof(diskPath))
          {
            strlcpy(diskPath, requestedPath, sizeof(diskPath));
            pathOk = true;
          }
        }
        else
        {
          strlcpy(diskPath, "/cpm/disks/A.dsk", sizeof(diskPath));
          diskPath[11] = (char)('A' + drive);
          pathOk = true;
        }

        if (pathOk)
        {
          FsFile diskFile = SD.open(diskPath, O_RDONLY);
          if (diskFile)
          {
            if (diskFile.seek(sector * (uint32_t)DISK_SECTOR_LEN))
            {
              readLen = diskFile.read(_buff, DISK_SECTOR_LEN);
              if (readLen == DISK_SECTOR_LEN)
              {
                int encodedLen = ourBase64.encode(_strBuff, (char *)_buff, readLen);
                if (encodedLen < 0)
                {
                  readLen = -1;
                  _strBuff[0] = 0;
                }
              }
            }
            diskFile.close();
          }
        }
      }

      JsonObject reply = jreply["disk_read"].to<JsonObject>();
      reply["drive"] = drive;
      reply["sector"] = sector;
      reply["len"] = readLen;
      reply["data"] = _strBuff;
    }

    else if (jdoc["disk_write"])
    {
      int drive = jdoc["disk_write"]["drive"] | -1;
      uint32_t sector = jdoc["disk_write"]["sector"] | 0;
      const char *requestedPath = jdoc["disk_write"]["path"] | "";
      const char *encoded = jdoc["disk_write"]["data"] | "";
      int decodedLen = ourBase64.decode((char *)_buff, (char *)encoded, strlen(encoded));
      int writeLen = -1;

      if (drive >= 0 && drive < 26 && decodedLen == DISK_SECTOR_LEN)
      {
        char diskPath[128] = {};
        bool pathOk = false;
        if (requestedPath[0])
        {
          const char *root = "/cpm/disks/";
          size_t pathLen = strlen(requestedPath);
          if (!strncmp(requestedPath, root, strlen(root)) &&
              !strstr(requestedPath, "..") && pathLen < sizeof(diskPath))
          {
            strlcpy(diskPath, requestedPath, sizeof(diskPath));
            pathOk = true;
          }
        }
        else
        {
          strlcpy(diskPath, "/cpm/disks/A.dsk", sizeof(diskPath));
          diskPath[11] = (char)('A' + drive);
          pathOk = true;
        }

        if (pathOk)
        {
          FsFile diskFile = SD.open(diskPath, O_RDWR);
          if (diskFile)
          {
            if (diskFile.seek(sector * (uint32_t)DISK_SECTOR_LEN))
            {
              writeLen = diskFile.write(_buff, DISK_SECTOR_LEN);
            }
            diskFile.close();
          }
        }
      }

      JsonObject reply = jreply["disk_write"].to<JsonObject>();
      reply["drive"] = drive;
      reply["sector"] = sector;
      reply["len"] = writeLen;
    }

    else if (jdoc["stat"])
    {
      //process file stat message
      JsonObject reply = jreply["stat"].to<JsonObject>();
      reply["fd"] = _currFd;
      reply["offset"] = 0;
    }

    else if (jdoc["lseek"])
    {
      //  int fd = jdoc["lseek"]["fd"] | -1;
      int offset = jdoc["lseek"]["offset"] | 0;
      //  int whence = jdoc["lseek"]["offset"] | 0;
      //whence:
      //          SEEK_SET        file_offset = offset
      //          SEEK_CUR        file_offset = current offset + offset
      //          SEEK_END        file_offset = size of file + offset
      //process file seek message
      int foffset = _currFile.seek(offset);

      // returns file_offset or error
      JsonObject reply = jreply["lseek"].to<JsonObject>();
      reply["fd"] = _currFd;
      reply["offset"] = foffset;
    }

    else if (jdoc["set_time"])
    {
      String time = jdoc["set_time"]["time"] | "";
      JsonObject reply = jreply["set_time"].to<JsonObject>();
      reply["ok"] = true;
    }

    else if (jdoc["cpm_image"]) {
      const char *action = jdoc["cpm_image"]["op"] | "";
      if (!strcmp(action, "begin")) {
        const char *name = jdoc["cpm_image"]["name"] | "";
        Cpm85Image::begin(name);
      }
      // "status" leaves the job unchanged. Both return current state.
      JsonObject out = jreply["cpm_image"].to<JsonObject>();
      Cpm85Image::status(out);
    }
    else if (jdoc["cpm_keys"]) {
      // Reply-only keyboard transport, serialized with existing disk UART
      // transactions; never write unsolicited bytes onto Serial2.
      Cpm85Drive::receiveAck(jdoc["cpm_keys"]["ack"].as<JsonVariantConst>());
      JsonObject out=jreply["cpm_keys"].to<JsonObject>();
      Cpm85Drive::offer(out);
      out["active"]=Cpm85Keys::isAttached();
      out["overflow"]=Cpm85Keys::overflowCount();
      JsonArray codes=out["keys"].to<JsonArray>();
      for(uint8_t i=0;i<Cpm85Keys::kBatchMax;++i) {
        int key=Cpm85Keys::pop();
        if(key<0)break;
        codes.add((uint8_t)key);
      }
    }
    else if (jdoc["keyboard"])
    {
      // expect an array of 1 or more keycodes (8bit) and insert them
      // into the keyboard queue where they get injected into the HP85
      //strncpy(_strBuff, jdoc["keyboard"]["data"], MAX_STR_LEN);
      //int len = ourBase64.decode((char *)_buff, _strBuff, strlen(_strBuff));
      //inject the keycodes in _buff to the keyboard buffer

      int ch = jdoc["keyboard"]["data"];
      kbuff.putch(ch);
      Serial.printf("Keycode from webpage: %0d\r\n",ch);
      /*for (int a = 0; a < len; a++)
        {
          kbuff.putch(_buff[a]);
        }*/

      JsonObject reply = jreply["keyboard"].to<JsonObject>();
      reply["ok"] = true;
    }

    //
    //  video screen request
    //  need to determine the machine 85/86/87
    //  and the video mode to encode the corrent length of framebuffer
    //  and to tag the data with the correct screen dimensions
    //  for the webpage to render
    //
    else if (jdoc["get_alpha"])
    {
      dumpCrtAlphaAsBase64(_strBuff, false);
      JsonObject reply = jreply["hpText"].to<JsonObject>();

      reply["isHP87"] = false; //only HP85 for the moment
      reply["image"] = _strBuff;
      reply["width"] = 32;
      reply["lines"] = 16;
      reply["machine"] = "HP85";
    }
    else if (jdoc["get_graph"])
    {
      dumpCrtAlphaAsBase64(_strBuff, true);
      Serial.printf("vbuff size:%d\r\n", strlen(_strBuff));
      JsonObject reply = jreply["hpGraph"].to<JsonObject>();
      reply["isHP87"] = false; //only HP85 for the moment
      reply["image"] = _strBuff;
      reply["width"] = 32;
      reply["lines"] = 16;
      reply["machine"] = "HP85";
    }
    //
    //  get sdcard directory
    //
    else if (jdoc["files"])
    {
      strncpy(_strBuff, jdoc["files"]["path"], MAX_STR_LEN);
      dl.beginDir(_strBuff);

      JsonObject reply = jreply["theFiles"].to<JsonObject>();
      reply["path"] = _strBuff;
      //  V6  was  JsonArray dirents = jreply.createNestedArray("dir");
      JsonArray dirents = jreply["dir"].to<JsonArray>();
      while (1)
      {
        if (dl.getNextLine(&dirent))
        {
                                                          //  V6  was  JsonObject dir = dirents.createNestedObject();
          JsonObject dir = dirents.add<JsonObject>();     //  V7

          dir["type"] = (dirent.is_directory) ? "dir" : "file";
          dir["name"] = dirent.dir_entry_text;
          dir["size"] = dirent.file_size;
          dir["hidden"] = dirent.is_hidden;
          dir["readonly"] = dirent.is_read_only;
        }
        else
        {
          break;
        }
      }
    }
    serializeJson(jreply, Serial2);
    //serializeJson(jreply,Serial);

    crcstream.clearCrc();
    serializeJson(jreply, crcstream);
    //Serial.printf("Crc value: %04X\r\n",crcstream.getCrc());
    Serial2.printf(",%d\r\n", crcstream.getCrc());
  }

private:
  int _lineNdx = 0;
  char _line[MAX_LINE];
  FsFile _currFile;
  int _currFd = -1;
  uint8_t _buff[MAX_READ_LEN];
  // Reusable generic stream staging frame. Always aligned 4 bytes for
  // consistency; Teensy SPI.transfer clocks bytes directly (not DMA).
  alignas(4) uint8_t _spiStreamFrame[EbtksStream::FRAME_BYTES] = {};
  char _spiStreamPath[120] = {};
  uint32_t _spiStreamFirst = 0;
  uint16_t _spiStreamCount = 0;
  uint16_t _spiStreamSession = 0;
  bool _spiStreamPrepared = false;

  // Dedicated 512-byte staging area for the opt-in read-only SPI SD test.
  // Kept between two separate UART commands; never used by CP/M disk I/O.
  uint8_t _spiSdSectorData[DISK_SECTOR_LEN] = {};
  uint32_t _spiSdSector = 0;
  uint16_t _spiSdCrc = 0;
  uint8_t _spiSdSequence = 0;
  bool _spiSdPrepared = false;
  char _strBuff[MAX_STR_LEN+2];
  CircBuff kbuff;
};
