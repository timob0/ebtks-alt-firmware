#pragma once

#include <Arduino.h>
#include <USBHost_t36.h>

/**
 * Minimal USBHost_t36 driver for the Seiko IFD501-01UK USB interface
 * observed as VID:PID 0619:0115.
 *
 * The driver claims the matching USB interface, finds bulk endpoint 0x01
 * for output and (when present) bulk endpoint 0x82 for input, and exposes
 * an Arduino Print-compatible API.
 */
class USBSIIPrinter : public USBDriver, public Print {
public:
    static constexpr uint16_t kVendorId = 0x0619;
    static constexpr uint16_t kProductId = 0x0115;
    static constexpr uint8_t kExpectedOutAddress = 0x01;
    static constexpr uint8_t kExpectedInAddress = 0x82;

    explicit USBSIIPrinter(USBHost &host) { (void)host; init(); }

    using Print::write;
    size_t write(uint8_t byte) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    /** Write raw bytes and wait for each USB transfer to complete. */
    size_t writeRaw(const void *buffer, size_t size, uint32_t timeoutMs = 1000);

    /** Wait until the currently queued output transfer has completed. */
    bool flushOutput(uint32_t timeoutMs = 1000);

    bool connected() const { return device != nullptr && txPipe_ != nullptr; }
    bool canReadStatus() const { return device != nullptr && rxPipe_ != nullptr; }
    bool busy() const { return txBusy_; }

    uint8_t interfaceNumber() const { return interfaceNumber_; }
    uint8_t outEndpointAddress() const { return txEndpointNumber_; }
    uint8_t inEndpointAddress() const {
        return rxEndpointNumber_ ? static_cast<uint8_t>(rxEndpointNumber_ | 0x80) : 0;
    }
    uint16_t outPacketSize() const { return txPacketSize_; }
    uint16_t inPacketSize() const { return rxPacketSize_; }

    int available() const;
    int read();
    int peek() const;
    void clearRx();

    /**
     * Send Seiko's GS a 7F status request and read a 4-byte little-endian
     * response. Returns false if no bulk IN endpoint is available or on timeout.
     */
    bool getStatus(uint32_t &status, uint32_t timeoutMs = 500);

    /** Last qTD token reported by the output transfer callback. */
    uint32_t lastTxToken() const { return lastTxToken_; }

protected:
    bool claim(Device_t *dev, int type, const uint8_t *descriptors, uint32_t len) override;
    void disconnect() override;
    void Task() override;

private:
    static constexpr size_t kTxBufferSize = 1024;
    static constexpr size_t kRxPacketBufferSize = 512;
    static constexpr size_t kRxRingSize = 512;

    void init();
    bool queueReceive();
    bool queueTransmit(const uint8_t *data, size_t length);

    static void txCallback(const Transfer_t *transfer);
    static void rxCallback(const Transfer_t *transfer);
    void txComplete(const Transfer_t *transfer);
    void rxComplete(const Transfer_t *transfer);
    void appendRx(const uint8_t *data, size_t length);

    Pipe_t myPipes_[2] __attribute__((aligned(32)));
    Transfer_t myTransfers_[8] __attribute__((aligned(32)));
    strbuf_t myStringBuffers_[1];

    Pipe_t *txPipe_ = nullptr;
    Pipe_t *rxPipe_ = nullptr;

    uint8_t interfaceNumber_ = 0xFF;
    uint8_t txEndpointNumber_ = 0;
    uint8_t rxEndpointNumber_ = 0;
    uint16_t txPacketSize_ = 0;
    uint16_t rxPacketSize_ = 0;

    uint8_t txBuffer_[kTxBufferSize] __attribute__((aligned(32)));
    uint8_t rxPacketBuffer_[kRxPacketBufferSize] __attribute__((aligned(32)));
    uint8_t rxRing_[kRxRingSize];

    volatile size_t rxHead_ = 0;
    volatile size_t rxTail_ = 0;
    volatile bool txBusy_ = false;
    volatile bool rxBusy_ = false;
    volatile uint32_t lastTxToken_ = 0;
};
