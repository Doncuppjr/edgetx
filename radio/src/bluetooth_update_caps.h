#pragma once
#include <stdint.h>
#include <string.h>

// Passive announcement only; never probe legacy CC2540 modules.
inline uint8_t bluetoothParseUpdateCaps(const char* line) {
  if (!line || strncmp(line, "OK+CAPS:1,", 10)) return 0;
  uint8_t caps = 0;
  const char* token = line + 10;
  while (*token) {
    size_t len = strcspn(token, ",");
    if (len == 11 && !strncmp(token, "UPDATE_WIFI", len)) caps |= 1;
    if (len == 10 && !strncmp(token, "UPDATE_BLE", len)) caps |= 2;
    if (len == 11 && !strncmp(token, "UPDATE_UART", len)) caps |= 4;
    token += len;
    if (*token == ',') ++token;
  }
  return caps;
}
