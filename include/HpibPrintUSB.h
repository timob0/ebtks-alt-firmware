#include <Arduino.h>
#include "HpibDevice.h"
#include "USBSIIPrinter.h"

#ifndef _HPIBPRINTUSB
#define _HPIBPRINTUSB

class HpibPrintUSB : public HpibDevice {
    public:
        HpibPrintUSB(int tla, Hpdev_t type);
        void identify();
        void processCmd(uint8_t* cmdBuff, int length);
        void onBurstWrite(uint8_t* buff, int length);
        int input();
        void tick();
        bool connectUSB(USBSIIPrinter* thePrinter);
        void disconnectUSB();
        char* getFilename(int diskNum);
        bool close(int diskNum);
        void flush();
        bool setFile(const char *fname);
        void atnOut(uint8_t val);
        uint8_t parallelPoll();
        bool setFile(int diskNum, const char* fname, bool wprot);
        bool addDisk(int type);
    private:
        bool _listen;
        bool _talk;
        uint8_t _prevHPIBCmd;
        int _readLen;
        uint32_t _flushTimer;
        USBSIIPrinter *printer;
};

#endif