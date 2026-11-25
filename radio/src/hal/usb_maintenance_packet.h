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


// A submitted IN transfer still owns its ring bytes until completion.
// Adopting a normal CDC transfer into maintenance must retain those bytes
// even though the normal producer already advanced its output pointer.
class UsbMaintenanceTxFlight {
 public:
  void reset() {length=0;start=0;advance=false;}
  void submitted(uint32_t offset,uint32_t count,bool held) {
    start=offset;length=count;advance=held;
  }
  void discardQueued(volatile uint32_t& out,volatile uint32_t& in,uint32_t size) {
    if (length) {
      out=start;in=(start+length)%size;advance=true;
    } else {out%=size;in=out;}
  }
  void complete(volatile uint32_t& out,uint32_t size) {
    if (advance) out=(start+length)%size;
    length=0;advance=false;
  }
 private:
  volatile uint32_t start=0,length=0;
  volatile bool advance=false;
};
