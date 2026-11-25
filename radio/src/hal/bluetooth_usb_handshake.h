#pragma once
#include <stdint.h>
#include <string.h>

// CDC can assert DTR before the host has disabled terminal echo. Require
// a query from the configured host before sending any banner or binary data.
class BluetoothUsbHandshake {
 public:
  enum Event { NONE, PROBE, STATUS, DOWNLOAD };
  void reset() { length=0; overflow=false; established=false; }
  bool ready() const { return established; }
  Event feed(uint8_t byte) {
    if (byte!='\n') {
      if (length<sizeof(line)-1) line[length++]=byte;
      else overflow=true;
      return NONE;
    }
    if (length && line[length-1]=='\r') --length;
    line[length]=0;
    Event event=NONE;
    if (!overflow) {
      if (!strcmp(line,"AT+BRIDGE?")) {established=true;event=PROBE;}
      else if (!strcmp(line,"AT+BRIDGE=STATUS?")) event=STATUS;
      else if (established && !strcmp(line,"AT+DOWNLOAD")) event=DOWNLOAD;
    }
    length=0;overflow=false;
    return event;
  }
 private:
  char line[32]={};
  uint8_t length=0;
  bool overflow=false, established=false;
};
