#pragma once
#include <stdint.h>

// A view of the USB endpoint buffer, not another FIFO. The caller must not
// re-arm OUT until pop() reports that the entire packet has been consumed.
class UsbMaintenancePacket {
 public:
  bool accept(uint8_t* data, uint32_t length) {
    if (pending()) return false;
    data_ = data; offset_ = 0; length_ = length; return true;
  }
  bool peek(uint8_t* byte) const {
    if (!pending()) return false;
    *byte = data_[offset_]; return true;
  }
  bool pop() {
    if (!pending()) return false;
    ++offset_; return !pending();
  }
  uint32_t pending() const { return length_ - offset_; }
  void clear() { offset_ = length_ = 0; }
 private:
  uint8_t* data_ = nullptr;
  volatile uint32_t offset_ = 0, length_ = 0;
};
