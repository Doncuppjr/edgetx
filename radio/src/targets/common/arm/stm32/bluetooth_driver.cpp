/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "bluetooth_driver.h"
#include "hal/gpio.h"

#include "stm32_hal_ll.h"
#include "stm32_gpio.h"
#include "stm32_serial_driver.h"

#include "board.h"
#include "debug.h"

#define BT_USART_IRQ_PRIORITY 6

static const stm32_usart_t btUSART = {
  .USARTx = BT_USART,
  .txGPIO = BT_TX_GPIO,
  .rxGPIO = BT_RX_GPIO,
  .IRQn = BT_USART_IRQn,
  .IRQ_Prio = BT_USART_IRQ_PRIORITY,
  .txDMA = nullptr,
  .txDMA_Stream = 0,
  .txDMA_Channel = 0,
  .rxDMA = nullptr,
  .rxDMA_Stream = 0,
  .rxDMA_Channel = 0,
};

DEFINE_STM32_SERIAL_PORT(BTModule, btUSART, BT_RX_FIFO_SIZE, BT_TX_FIFO_SIZE);

#if defined(BLUETOOTH_PROBE)
volatile uint8_t btChipPresent = 0;
#endif

void* _bt_usart_ctx = nullptr;
static volatile bool btMaintenance = false;

void bluetoothInit(uint32_t baudrate, bool enable)
{
  if (btMaintenance) return;
#if defined(BT_EN_GPIO)
  gpio_init(BT_EN_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif
#if defined(BT_PWR_GPIO)
  gpio_init(BT_PWR_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

  etx_serial_init cfg = {
    .baudrate = baudrate,
    .encoding = ETX_Encoding_8N1,
    .direction = ETX_Dir_TX_RX,
    .polarity = ETX_Pol_Normal,
  };

  if (!_bt_usart_ctx) {
    auto hw_def = REF_STM32_SERIAL_PORT(BTModule);
    _bt_usart_ctx = STM32SerialDriver.init(hw_def, &cfg);
  } else {
    STM32SerialDriver.setBaudrate(_bt_usart_ctx, baudrate);
  }

#if defined(BT_EN_GPIO)
  gpio_write(BT_EN_GPIO, !enable);
#endif
#if defined(BT_PWR_GPIO)
  gpio_set(BT_PWR_GPIO);
#endif
}

void bluetoothDisable()
{
  if (btMaintenance) return;
#if defined(BT_EN_GPIO)
  // close bluetooth (recent modules will go to bootloader mode)
  gpio_set(BT_EN_GPIO);
  // MCU-only reset may leave this pin in input mode; drive the module
  // disable level before starting the next AT handshake.
  gpio_init(BT_EN_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif
#if defined(BT_PWR_GPIO)
  gpio_clear(BT_PWR_GPIO);
#endif
  if (_bt_usart_ctx) {
    STM32SerialDriver.deinit(_bt_usart_ctx);
    _bt_usart_ctx = nullptr;
  }
}

void bluetoothWrite(const void* buffer, uint32_t len)
{
  if (!_bt_usart_ctx || btMaintenance) return;
  STM32SerialDriver.sendBuffer(_bt_usart_ctx, (const uint8_t*)buffer, len);
}

int bluetoothRead(uint8_t* data)
{
  if (!_bt_usart_ctx || btMaintenance) return 0;
  return STM32SerialDriver.getByte(_bt_usart_ctx, data);
}

uint8_t bluetoothIsWriting(void)
{
  if (!_bt_usart_ctx)
    return false;

  return !STM32SerialDriver.txCompleted(_bt_usart_ctx);
}

#if defined(USB_SERIAL)
#include "hal/usb_driver.h"
#include <string.h>
#include <stdio.h>
#include "hal/bluetooth_usb_handshake.h"

// Separate buffers exist only for maintenance; normal Trainer/Telemetry
// retains its 64-byte TX / 256-byte RX configuration.
DEFINE_STM32_SERIAL_PORT(BTMaintenance, btUSART, 256, 64);
static BluetoothUsbStats btUsbCounters = {};
static volatile bool btUsbRom = false, btUsbRecover = false, btUsbFinish = false;
static bool btUsbWasOpen = false, btUsbRtsLow = false;
static volatile bool btUsbSessionSeen = false;
static uint32_t btUsbLastCycle = 0, btUsbOverrunBase = 0, btUsbErrorBase = 0;
static BluetoothUsbHandshake btUsbHandshake;

static uint32_t btCyclesUs(uint32_t cycles) {
  return cycles / (SystemCoreClock / 1000000);
}
static void btMax(uint32_t& value, uint32_t candidate) {
  if (candidate > value) value = candidate;
}
static void bluetoothUsbPoll()
{
  if (!btMaintenance || !_bt_usart_ctx) return;
  bool open = usbSerialMaintenanceOpen();
  if (open != btUsbWasOpen) {
    // The first OUT packet may arrive before this SOF observes DTR.
    // Discard only on close; preserve a newly arrived host query on open.
    if (!open) usbSerialMaintenanceDiscard();
    btUsbHandshake.reset(); btUsbRtsLow = false; btUsbLastCycle = 0;
    if (open) {
      STM32SerialDriver.clearRxBuffer(_bt_usart_ctx);
      btUsbOverrunBase = stm32SerialRxOverruns(_bt_usart_ctx);
      btUsbErrorBase = stm32SerialRxErrors(_bt_usart_ctx);
      // New host connection explicitly retries a retained ROM session.
      btUsbRecover = false;
    } else if (btUsbRom) btUsbRecover = true;
    btUsbWasOpen = open;
  }
  if (!open) return;
  if (!usbSerialMaintenanceRts()) btUsbRtsLow = true;
  // This is a host completion request, not a declaration of update success.
  // The radio still requires a successful normal module handshake.
  if (btUsbRom && btUsbHandshake.ready() && btUsbRtsLow && usbSerialMaintenanceRts()) {
    btUsbFinish = true; return;
  }
  if (btUsbRecover) return;
  uint32_t cycle = DWT->CYCCNT;
  if (btUsbLastCycle)
    btMax(btUsbCounters.serviceGapMaxUs, btCyclesUs(cycle - btUsbLastCycle));
  btUsbLastCycle = cycle;
  auto pending = usbSerialMaintenancePending();
  btMax(btUsbCounters.usbPendingHighWater, pending);
  btMax(btUsbCounters.hostQueueMaxUs, btCyclesUs(usbSerialMaintenanceAgeCycles()));
  btMax(btUsbCounters.moduleQueueMaxUs, btCyclesUs(stm32SerialRxAgeCycles(_bt_usart_ctx)));
  btUsbCounters.rxHighWater = stm32SerialRxHighWater(_bt_usart_ctx);
  btUsbCounters.overruns = stm32SerialRxOverruns(_bt_usart_ctx);
  btUsbCounters.uartErrors = stm32SerialRxErrors(_bt_usart_ctx);
  if (btUsbCounters.overruns != btUsbOverrunBase ||
      btUsbCounters.uartErrors != btUsbErrorBase) {
    btUsbRecover = true; return;
  }
  // Do not enter the ROM automatically. The first complete host line must
  // explicitly request DOWNLOAD after the UI/AT authorization handshake.
  uint8_t byte;
  for (unsigned budget = 0; budget < 64; ++budget) {
    if (!usbSerialMaintenancePeek(&byte)) break;
    if (!btUsbHandshake.ready() || !btUsbRom) {
      usbSerialMaintenanceConsume();
      auto event = btUsbHandshake.feed(byte);
      if (event == BluetoothUsbHandshake::PROBE) {
        btUsbSessionSeen = true;
        const char* banner = btUsbRom ? "OK+BRIDGE:1,115200,RECOVERY\r\n" :
                                       "OK+BRIDGE:1,115200,READY\r\n";
        for (const char* c=banner; *c; ++c) usbSerialMaintenanceWrite(*c);
      } else if (event == BluetoothUsbHandshake::STATUS) {
        char status[240];
        snprintf(status,sizeof(status),
          "OK+BRIDGE:STATUS,tx=%lu,rx=%lu,overrun=%lu,error=%lu,txPending=%lu,rxHwm=%lu,txHwm=%lu,gapUs=%lu,rom=%u\r\n",
          (unsigned long)btUsbCounters.toModule,(unsigned long)btUsbCounters.toHost,
          (unsigned long)btUsbCounters.overruns,(unsigned long)btUsbCounters.uartErrors,
          (unsigned long)stm32SerialTxPending(_bt_usart_ctx),
          (unsigned long)btUsbCounters.rxHighWater,(unsigned long)btUsbCounters.txHighWater,
          (unsigned long)btUsbCounters.serviceGapMaxUs,(unsigned)btUsbRom);
        for (const char* c=status; *c; ++c) usbSerialMaintenanceWrite(*c);
      } else if (event == BluetoothUsbHandshake::DOWNLOAD && !btUsbRom) {
        const char* command = "AT+DOWNLOAD\r\n";
        while (*command) {
          if (!stm32SerialTrySendByte(_bt_usart_ctx, *command++)) {
            btUsbRecover = true; return;
          }
          ++btUsbCounters.toModule;
        }
        btUsbRom = true;
      }
    } else {
      if (!stm32SerialTrySendByte(_bt_usart_ctx, byte)) break;
      usbSerialMaintenanceConsume();
      ++btUsbCounters.toModule;
    }
  }
  btMax(btUsbCounters.txHighWater, stm32SerialTxPending(_bt_usart_ctx));
  if (!btUsbHandshake.ready()) return;
  for (unsigned budget = 0; budget < 256 && usbSerialFreeSpace(); ++budget) {
    if (!STM32SerialDriver.getByte(_bt_usart_ctx, &byte)) break;
    if (!usbSerialMaintenanceWrite(byte)) { btUsbRecover = true; break; }
    ++btUsbCounters.toHost;
  }
}
bool bluetoothUsbStart()
{
  if (!_bt_usart_ctx || btMaintenance) return false;
  uint32_t prim = __get_PRIMASK(); __disable_irq();
  STM32SerialDriver.deinit(_bt_usart_ctx);
  etx_serial_init cfg = {115200, ETX_Encoding_8N1, ETX_Dir_TX_RX, ETX_Pol_Normal};
  _bt_usart_ctx = STM32SerialDriver.init(REF_STM32_SERIAL_PORT(BTMaintenance), &cfg);
  btUsbCounters = {};
  btUsbRom = btUsbRecover = btUsbFinish = btUsbWasOpen = btUsbRtsLow = false;
  btUsbLastCycle = 0; btUsbHandshake.reset(); btUsbSessionSeen = false;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  btMaintenance = _bt_usart_ctx != nullptr;
  if (btMaintenance) usbSerialMaintenanceBegin(bluetoothUsbPoll);
  if (!prim) __enable_irq();
  return btMaintenance;
}
void bluetoothUsbStop()
{
  uint32_t prim = __get_PRIMASK(); __disable_irq();
  usbSerialMaintenanceEnd();
  if (_bt_usart_ctx) STM32SerialDriver.deinit(_bt_usart_ctx);
  _bt_usart_ctx = nullptr; btMaintenance = false;
  if (!prim) __enable_irq();
}
bool bluetoothUsbOpen() { return usbSerialMaintenanceOpen(); }
bool bluetoothUsbSessionStarted() { return btUsbSessionSeen; }
bool bluetoothUsbRomRequested() { return btUsbRom; }
bool bluetoothUsbFinishRequested() { return btUsbFinish; }
bool bluetoothUsbRecovery() { return btUsbRecover; }
void bluetoothUsbSetRecovery() { btUsbRecover = true; }
BluetoothUsbStats bluetoothUsbStats()
{
  uint32_t prim = __get_PRIMASK(); __disable_irq();
  auto result = btUsbCounters;
  if (!prim) __enable_irq();
  return result;
}
#endif
