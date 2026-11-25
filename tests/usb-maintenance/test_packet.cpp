#include "../../radio/src/hal/usb_maintenance_packet.h"
#include <assert.h>
#include <stdint.h>
#include <vector>
int main() {
  UsbMaintenancePacket p; uint8_t byte;
  assert(!p.peek(&byte)); assert(!p.pop());
  std::vector<uint8_t> expected, actual;
  uint8_t packet[512];
  // Exercise actual endpoint ownership with FS/HS packet sizes, stalls,
  // repeated transactions, and every possible byte value (including SLIP).
  for (unsigned round=0; round<2000; ++round) {
    unsigned len=round%513;
    for(unsigned i=0;i<len;++i) {packet[i]=(round+i)%256;expected.push_back(packet[i]);}
    assert(p.accept(packet,len));
    if(len) assert(!p.accept(packet,1));
    unsigned remaining=len;
    while(p.peek(&byte)) {
      uint8_t again;assert(p.peek(&again)&&again==byte); // TX full: no pop
      actual.push_back(byte);
      assert(p.pop()==(--remaining==0));
    }
    assert(!p.pending());
  }
  assert(actual==expected);
  assert(p.accept(packet,64));p.clear();assert(!p.pending());
}
