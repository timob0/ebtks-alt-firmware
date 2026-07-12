#include <Arduino.h>
#include "HpibPrintUSB.h"

HpibPrintUSB::HpibPrintUSB(int tla, Hpdev_t type) : HpibDevice{ tla, HPDEV_PRTUSB } {
    printer = NULL;
}

// called when there is a HPIB identify request
void HpibPrintUSB::identify() {}

// called when we've received a complete device level command
void HpibPrintUSB::processCmd(uint8_t* cmdBuff, int length) {
    if (printer && _listen) {
        printer->write(cmdBuff, length);
        _flushTimer = 50; //5 seconds
    }
}

void HpibPrintUSB::onBurstWrite(uint8_t* buff, int length) {}

int HpibPrintUSB::input() {
    return _readLen;
}

//
//  manage the print file flush. After X time has elapsed, flush the print
//  file if it is currently open
//
void HpibPrintUSB::tick() {
    if (_flushTimer) {
        _flushTimer--;
        if (_flushTimer == 0) {
            flush();
        }
    }
}

bool HpibPrintUSB::connectUSB(USBSIIPrinter* thePrinter) {
    printer = thePrinter;
    return true;
}

void HpibPrintUSB::disconnectUSB() {
    printer = NULL;
}

char* HpibPrintUSB::getFilename(int diskNum) {
    return NULL;
}

bool HpibPrintUSB::close(int diskNum) {
    return true;
}

void HpibPrintUSB::flush() {
    if (printer) printer->flush();
}

bool HpibPrintUSB::setFile(const char *fname) {
    return true;
}

//  called with a byte from hpib that is ATN
#define HPIB_UNT 0x5f
#define HPIB_UNL 0x3f
void HpibPrintUSB::atnOut(uint8_t val) {
    uint8_t val7 = val & 0x7f; //strip parity

    if ((val7 >= 0x20) && (val7 < 0x3f)) {
        //LAD listen address
        if ((val & 0x1f) == _tla) {
            _listen = true;
        }
    }
    else if (val7 == HPIB_UNL) {
        //UNL unlisten
        _listen = false;
    }
    else if ((val7 >= 0x40) && (val7 < 0x5f)) {
        //TAD talk address
        if ((val & 0x1f) == _tla) {
            _talk = true;
        }
    }
    else if (val7 == HPIB_UNT) {
        //UNT
        _talk = false;
    }
    else if ((val7 >= 0x60) && (val7 < 0x7f)) {
        //  secondary address
        //  Serial.printf("HpibPrint.h: SAD %02X\r\n", val7);
        if (_prevHPIBCmd == HPIB_UNT) //if UNT then secondary
        {
            // identify command

            if ((val7 & 0x1f) == _tla) //only respond if we're addressed
            {
                //Serial.printf("HpibPrint.h: Identify device:%d\r\n", _tla);
                identify();
            }
        }
    }
    _prevHPIBCmd = val7;
}

uint8_t HpibPrintUSB::parallelPoll() {
    return 0;
}

bool HpibPrintUSB::setFile(int diskNum, const char* fname, bool wprot) {
    return false;
}

bool HpibPrintUSB::addDisk(int type) {
    return false;
}
