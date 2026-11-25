#include "../../radio/src/hal/usb_maintenance_packet.h"
#include <assert.h>
#include <stdint.h>
#include <vector>
#include "../../radio/src/hal/bluetooth_usb_handshake.h"
int main() {
  assert(!bluetoothUsbRestartTimedOut<uint32_t>(1999,0,false));
  assert(bluetoothUsbRestartTimedOut<uint32_t>(2000,0,false));
  assert(bluetoothUsbRestartTimedOut<uint32_t>(1,0,true));
  assert(!bluetoothUsbRestartTimedOut<uint16_t>(1982,65520,false));
  assert(bluetoothUsbRestartTimedOut<uint16_t>(1984,65520,false));
  assert(bluetoothUsbRestartTimedOut<uint32_t>(1984,0xfffffff0,false));
  // Normal CDC advances out on submission; maintenance must adopt its
  // in-flight bytes, drop only queued bytes, and keep DMA ownership on exit.
  UsbMaintenanceTxFlight flight;
  volatile uint32_t txOut=100,txIn=220;
  assert(!flight.drained(txOut,txIn));
  flight.submitted(100,64,false);txOut=164;
  assert(!flight.drained(txOut,txOut)); // submitted bytes still own DMA
  flight.discardQueued(txOut,txIn,256);
  assert(txOut==100 && txIn==164);
  flight.discardQueued(txOut,txIn,256); // end before transfer completes
  flight.complete(txOut,256);
  assert(txOut==164 && txIn==164);
  assert(flight.drained(txOut,txIn));
  txIn=180;assert(!flight.drained(txOut,txIn)); // queued completion ACK
  txOut=240;txIn=17;
  flight.submitted(240,16,true);
  flight.discardQueued(txOut,txIn,256);
  assert(txOut==240 && txIn==0);
  flight.complete(txOut,256);assert(txOut==0);
  flight.complete(txOut,256);assert(txOut==0);
  flight.discardQueued(txOut,txIn,256);assert(txIn==0);
  BluetoothUsbHandshake handshake;
  auto feed=[&](const char* line) {
    BluetoothUsbHandshake::Event event=BluetoothUsbHandshake::NONE;
    while (*line) event=handshake.feed(*line++);
    return event;
  };
  assert(feed("AT+BRIDGE=FINISH\r\n")==BluetoothUsbHandshake::FINISH);
  assert(!handshake.ready()); // completion stays on the management channel
  assert(feed("AT+DOWNLOAD\r\n")==BluetoothUsbHandshake::NONE);
  assert(feed("OK+BRIDGE:1,115200,READY\r\n")==BluetoothUsbHandshake::NONE);
  assert(!handshake.ready());
  assert(feed("AT+BRIDGE=STATUS?\r\n")==BluetoothUsbHandshake::STATUS);
  assert(!handshake.ready());
  assert(feed("AT+BRIDGE?\r\n")==BluetoothUsbHandshake::PROBE);
  assert(handshake.ready());
  assert(feed("AT+BRIDGE=FINISH\r\n")==BluetoothUsbHandshake::NONE);
  assert(feed("AT+DOWNLOAD\r\n")==BluetoothUsbHandshake::DOWNLOAD);
  handshake.reset();
  assert(!handshake.ready());
  assert(feed("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxAT+BRIDGE?\n")==BluetoothUsbHandshake::NONE);
  assert(feed("AT+DOWNLOAD\n")==BluetoothUsbHandshake::NONE);
  assert(feed("AT+BRIDGE?\n")==BluetoothUsbHandshake::PROBE);
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
