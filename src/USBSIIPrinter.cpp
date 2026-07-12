#include "USBSIIPrinter.h"

#include <cstring>

namespace {
constexpr uint8_t kDescriptorTypeInterface = 4;
constexpr uint8_t kDescriptorTypeEndpoint = 5;
constexpr uint8_t kTransferTypeBulk = 2;
constexpr uint8_t kDirectionInMask = 0x80;
constexpr uint8_t kEndpointNumberMask = 0x0F;
} // namespace

void USBSIIPrinter::init() {
    contribute_Pipes(myPipes_, sizeof(myPipes_) / sizeof(myPipes_[0]));
    contribute_Transfers(myTransfers_, sizeof(myTransfers_) / sizeof(myTransfers_[0]));
    contribute_String_Buffers(
        myStringBuffers_, sizeof(myStringBuffers_) / sizeof(myStringBuffers_[0]));
    driver_ready_for_device(this);
}

bool USBSIIPrinter::claim(Device_t *dev,
                          int type,
                          const uint8_t *descriptors,
                          uint32_t len) {
    // Claim at interface level only. USBHost_t36 uses type 1 for this.
    if (type != 1 || dev == nullptr || descriptors == nullptr || len < 9) {
        return false;
    }

    if (dev->idVendor != kVendorId || dev->idProduct != kProductId) {
        return false;
    }

    if (descriptors[0] < 9 || descriptors[1] != kDescriptorTypeInterface) {
        return false;
    }

    uint8_t interfaceNumber = descriptors[2];
    uint8_t outEndpointNumber = 0;
    uint8_t inEndpointNumber = 0;
    uint16_t outPacketSize = 0;
    uint16_t inPacketSize = 0;

    const uint8_t *p = descriptors;
    const uint8_t *const end = descriptors + len;

    while (p + 2 <= end) {
        const uint8_t descriptorLength = p[0];
        const uint8_t descriptorType = p[1];

        if (descriptorLength < 2 || p + descriptorLength > end) {
            return false;
        }

        if (descriptorType == kDescriptorTypeInterface && descriptorLength >= 9) {
            interfaceNumber = p[2];
        } else if (descriptorType == kDescriptorTypeEndpoint && descriptorLength >= 7) {
            const uint8_t endpointAddress = p[2];
            const uint8_t transferType = p[3] & 0x03;
            const uint16_t maxPacketSize =
                static_cast<uint16_t>(p[4]) |
                (static_cast<uint16_t>(p[5]) << 8);

            if (transferType == kTransferTypeBulk) {
                if (endpointAddress == kExpectedOutAddress) {
                    outEndpointNumber = endpointAddress & kEndpointNumberMask;
                    outPacketSize = maxPacketSize;
                } else if (endpointAddress == kExpectedInAddress) {
                    inEndpointNumber = endpointAddress & kEndpointNumberMask;
                    inPacketSize = maxPacketSize;
                }
            }
        }

        p += descriptorLength;
    }

    // Printing requires bulk OUT endpoint 0x01.
    if (outEndpointNumber == 0 || outPacketSize == 0) {
        return false;
    }

    Pipe_t *outPipe = new_Pipe(
        dev, kTransferTypeBulk, outEndpointNumber, 0, outPacketSize);
    if (outPipe == nullptr) {
        return false;
    }

    Pipe_t *inPipe = nullptr;
    if (inEndpointNumber != 0 && inPacketSize != 0 &&
        inPacketSize <= kRxPacketBufferSize) {
        inPipe = new_Pipe(
            dev, kTransferTypeBulk, inEndpointNumber, 1, inPacketSize);
        // Failure to allocate the optional input pipe must not prevent printing.
    }

    interfaceNumber_ = interfaceNumber;
    txEndpointNumber_ = outEndpointNumber;
    txPacketSize_ = outPacketSize;
    rxEndpointNumber_ = inPipe ? inEndpointNumber : 0;
    rxPacketSize_ = inPipe ? inPacketSize : 0;

    txPipe_ = outPipe;
    rxPipe_ = inPipe;
    txPipe_->callback_function = txCallback;
    txPipe_->error_callback_function = txCallback;

    if (rxPipe_ != nullptr) {
        rxPipe_->callback_function = rxCallback;
        rxPipe_->error_callback_function = rxCallback;
    }

    txBusy_ = false;
    rxBusy_ = false;
    lastTxToken_ = 0;
    clearRx();

    // USBHost_t36 binds 'device' after claim() returns true. Task() starts RX.
    return true;
}

void USBSIIPrinter::disconnect() {
    txPipe_ = nullptr;
    rxPipe_ = nullptr;
    txBusy_ = false;
    rxBusy_ = false;
    interfaceNumber_ = 0xFF;
    txEndpointNumber_ = 0;
    rxEndpointNumber_ = 0;
    txPacketSize_ = 0;
    rxPacketSize_ = 0;
    clearRx();
    // USBHost_t36 clears device as part of its disconnect handling.
}

void USBSIIPrinter::Task() {
    // Keep one asynchronous status/input transfer queued when IN exists.
    if (device != nullptr && rxPipe_ != nullptr && !rxBusy_) {
        queueReceive();
    }
}

size_t USBSIIPrinter::write(uint8_t byte) {
    return writeRaw(&byte, 1);
}

size_t USBSIIPrinter::write(const uint8_t *buffer, size_t size) {
    return writeRaw(buffer, size);
}

size_t USBSIIPrinter::writeRaw(const void *buffer,
                               size_t size,
                               uint32_t timeoutMs) {
    if (buffer == nullptr || size == 0 || !connected()) {
        return 0;
    }

    const uint8_t *source = static_cast<const uint8_t *>(buffer);
    size_t total = 0;

    while (total < size && connected()) {
        if (!flushOutput(timeoutMs)) {
            break;
        }

        const size_t remaining = size - total;
        const size_t chunk =
            remaining > kTxBufferSize ? kTxBufferSize : remaining;

        if (!queueTransmit(source + total, chunk)) {
            break;
        }

        if (!flushOutput(timeoutMs)) {
            break;
        }

        // The callback has completed the queued transfer. USBHost_t36's
        // transfer token retains the remaining-byte count in bits 16..30.
        const size_t bytesRemaining = (lastTxToken_ >> 16) & 0x7FFF;
        const size_t bytesTransferred =
            bytesRemaining <= chunk ? chunk - bytesRemaining : 0;
        total += bytesTransferred;

        if (bytesTransferred != chunk) {
            break;
        }
    }

    return total;
}

bool USBSIIPrinter::queueTransmit(const uint8_t *data, size_t length) {
    if (!connected() || data == nullptr || length == 0 ||
        length > kTxBufferSize || txBusy_) {
        return false;
    }

    std::memcpy(txBuffer_, data, length);
    lastTxToken_ = 0;
    txBusy_ = true;

    if (!queue_Data_Transfer(
            txPipe_, txBuffer_, static_cast<uint32_t>(length), this)) {
        txBusy_ = false;
        return false;
    }

    return true;
}

bool USBSIIPrinter::flushOutput(uint32_t timeoutMs) {
    const uint32_t started = millis();

    while (txBusy_) {
        USBHost::Task();
        yield();

        if (!connected()) {
            return false;
        }

        if (static_cast<uint32_t>(millis() - started) >= timeoutMs) {
            return false;
        }
    }

    return true;
}

void USBSIIPrinter::txCallback(const Transfer_t *transfer) {
    if (transfer != nullptr && transfer->driver != nullptr) {
        static_cast<USBSIIPrinter *>(transfer->driver)->txComplete(transfer);
    }
}

void USBSIIPrinter::txComplete(const Transfer_t *transfer) {
    lastTxToken_ = transfer->qtd.token;
    txBusy_ = false;
}

bool USBSIIPrinter::queueReceive() {
    if (device == nullptr || rxPipe_ == nullptr || rxBusy_ ||
        rxPacketSize_ == 0 || rxPacketSize_ > kRxPacketBufferSize) {
        return false;
    }

    rxBusy_ = true;
    if (!queue_Data_Transfer(
            rxPipe_, rxPacketBuffer_, rxPacketSize_, this)) {
        rxBusy_ = false;
        return false;
    }

    return true;
}

void USBSIIPrinter::rxCallback(const Transfer_t *transfer) {
    if (transfer != nullptr && transfer->driver != nullptr) {
        static_cast<USBSIIPrinter *>(transfer->driver)->rxComplete(transfer);
    }
}

void USBSIIPrinter::rxComplete(const Transfer_t *transfer) {
    const uint32_t remaining = (transfer->qtd.token >> 16) & 0x7FFF;
    const size_t received =
        remaining <= transfer->length ? transfer->length - remaining : 0;

    if (received > 0 && transfer->buffer != nullptr) {
        appendRx(static_cast<const uint8_t *>(transfer->buffer), received);
    }

    rxBusy_ = false;
    // Task() queues the next input transfer outside interrupt context.
}

void USBSIIPrinter::appendRx(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        size_t head = rxHead_;
        size_t next = head + 1;
        if (next >= kRxRingSize) {
            next = 0;
        }

        // If full, discard the oldest byte.
        if (next == rxTail_) {
            size_t tail = rxTail_ + 1;
            if (tail >= kRxRingSize) {
                tail = 0;
            }
            rxTail_ = tail;
        }

        rxRing_[head] = data[i];
        rxHead_ = next;
    }
}

int USBSIIPrinter::available() const {
    const size_t head = rxHead_;
    const size_t tail = rxTail_;

    if (head >= tail) {
        return static_cast<int>(head - tail);
    }
    return static_cast<int>(kRxRingSize - tail + head);
}

int USBSIIPrinter::peek() const {
    const size_t head = rxHead_;
    const size_t tail = rxTail_;
    if (head == tail) {
        return -1;
    }
    return rxRing_[tail];
}

int USBSIIPrinter::read() {
    const size_t head = rxHead_;
    size_t tail = rxTail_;
    if (head == tail) {
        return -1;
    }

    const uint8_t value = rxRing_[tail];
    ++tail;
    if (tail >= kRxRingSize) {
        tail = 0;
    }
    rxTail_ = tail;
    return value;
}

void USBSIIPrinter::clearRx() {
    rxHead_ = 0;
    rxTail_ = 0;
}

bool USBSIIPrinter::getStatus(uint32_t &status, uint32_t timeoutMs) {
    status = 0;
    if (!canReadStatus()) {
        return false;
    }

    clearRx();

    // Same status request used by Seiko's Linux communication library.
    static constexpr uint8_t command[] = {0x1D, 0x61, 0x7F};
    if (writeRaw(command, sizeof(command), timeoutMs) != sizeof(command)) {
        return false;
    }

    const uint32_t started = millis();
    while (available() < 4) {
        USBHost::Task();
        yield();

        if (!canReadStatus()) {
            return false;
        }
        if (static_cast<uint32_t>(millis() - started) >= timeoutMs) {
            return false;
        }
    }

    status |= static_cast<uint32_t>(read()) << 0;
    status |= static_cast<uint32_t>(read()) << 8;
    status |= static_cast<uint32_t>(read()) << 16;
    status |= static_cast<uint32_t>(read()) << 24;
    return true;
}
