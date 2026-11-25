#include "../../radio/src/bluetooth_update_caps.h"
#include <assert.h>
int main() {
 assert(bluetoothParseUpdateCaps(nullptr)==0);
 assert(bluetoothParseUpdateCaps("Central:123456789012")==0);
 assert(bluetoothParseUpdateCaps("OK+Txpw:2")==0);
 assert(bluetoothParseUpdateCaps("OK+CAPS:2,UPDATE_UART")==0);
 assert(bluetoothParseUpdateCaps("OK+CAPS:1,UPDATE_UART_EXTRA")==0);
 assert(bluetoothParseUpdateCaps("OK+CAPS:1,UPDATE_WIFI,UPDATE_BLE")==3);
 assert(bluetoothParseUpdateCaps("OK+CAPS:1,UPDATE_WIFI,UPDATE_BLE,UPDATE_UART")==7);
 assert(bluetoothParseUpdateCaps("OK+CAPS:1,OTHER,UPDATE_UART,UPDATE_UART")==4);
}
