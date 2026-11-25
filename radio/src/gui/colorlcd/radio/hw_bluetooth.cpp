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

#include "hw_bluetooth.h"

#include "edgetx.h"

#define SET_DIRTY() storageDirty(EE_GENERAL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(3),
                                     LV_GRID_TEMPLATE_LAST};

static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

class BTDetailsDialog : public BaseDialog
{
 public:
  BTDetailsDialog() : BaseDialog(STR_BLUETOOTH, true)
  {
    FlexGridLayout grid(col_dsc, row_dsc);

    if (g_eeGeneral.bluetoothMode == BLUETOOTH_TELEMETRY) {
      auto line = form->newLine(grid);
      new StaticText(line, rect_t{}, STR_BLUETOOTH_PIN_CODE);
      new StaticText(line, rect_t{}, "000000");
    }

    // Local MAC
    auto line = form->newLine(grid);
    new StaticText(line, rect_t{}, STR_BLUETOOTH_LOCAL_ADDR);
    new DynamicText(
        line, rect_t{},
        [=]() {
          return std::string(bluetooth.localAddr[0] ? bluetooth.localAddr
                                                    : "---");
        });

    // Remote MAC
    line = form->newLine(grid);
    new StaticText(line, rect_t{}, STR_BLUETOOTH_DIST_ADDR);
    new DynamicText(
        line, rect_t{},
        [=]() {
          return std::string(bluetooth.distantAddr[0] ? bluetooth.distantAddr
                                                      : "---");
        });
  }
};

// Only this specific OTA dialog uses Back as a maintenance cancel.
// Navigating Back anywhere else retains the ordinary EdgeTX behavior.
class BluetoothOtaDialog : public DynamicMessageDialog
{
 public:
  BluetoothOtaDialog()
    : DynamicMessageDialog("Bluetooth update", []() {
        if (bluetooth.updateFailed) return std::string("Update unavailable");
        if (bluetooth.updateMethod == Bluetooth::UPDATE_NONE)
          return std::string("Update canceled. Returning to Bluetooth.");
        if (bluetooth.updateMethod == Bluetooth::UPDATE_UART) {
          switch (bluetooth.usbPhase) {
            case Bluetooth::USB_WAITING: return std::string("Connect radio USB to the browser installer.");
            case Bluetooth::USB_RUNNING: return std::string("USB forwarding. Keep USB connected.");
            case Bluetooth::USB_RECOVERY: return std::string("Update not confirmed. Reconnect installer to retry, or power-cycle module.");
            case Bluetooth::USB_RESTARTING: return std::string("Checking module restart...");
            case Bluetooth::USB_RESTART_FAILED: return std::string("Restart not confirmed. Power-cycle the radio.");
            case Bluetooth::USB_RESTORED: return std::string("Module restarted. Bluetooth restored.");
            default: break;
          }
        }
        if (!bluetooth.updatePin[0]) return std::string("Preparing module...");
        return std::string(bluetooth.updateMethod == Bluetooth::UPDATE_WIFI ? "Wi-Fi password: BT" : "Bluetooth PIN: ") + bluetooth.updatePin;
      }, "Back closes this screen.")
  {
  }

 protected:
  void onCancel() override
  {
    // Close on the first Back event. The Bluetooth task owns cancellation,
    // including preserving an interrupted ROM session for installer retry.
    bluetooth.requestOtaCancel();
    deleteLater();
  }
  void onClicked() override { onCancel(); }
};

BluetoothConfigWindow::BluetoothConfigWindow(Window* parent, FlexGridLayout& grid)
{
  auto line = parent->newLine(grid);
  line->padLeft(PAD_SMALL);
  new StaticText(line, rect_t{}, STR_MODE);

  auto box = new Window(line, rect_t{});
  box->padAll(PAD_TINY);
  box->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_SMALL);
  lv_obj_set_style_grid_cell_x_align(box->getLvObj(), LV_GRID_ALIGN_STRETCH, 0);
  lv_obj_set_style_flex_cross_place(box->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);

  new Choice(
      box, rect_t{}, STR_BLUETOOTH_MODES, BLUETOOTH_OFF, BLUETOOTH_TRAINER,
      GET_DEFAULT(g_eeGeneral.bluetoothMode), [=](int value) {
        g_eeGeneral.bluetoothMode = value;
        settingsBtn->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);
        otaBtn->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);
        nameEdit->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);
      });

  settingsBtn =
      new TextButton(box, rect_t{}, LV_SYMBOL_SETTINGS, [=]() -> uint8_t {
        new BTDetailsDialog();
        return 0;
      });
  settingsBtn->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);

  // Optional protocol extension: the module may support AT+OTA.
  // Confirmation is required because Trainer/Telemetry will stop.
  otaBtn = new TextButton(box, rect_t{}, "Update module",
      [=]() -> uint8_t {
        auto menu = new Menu();
        menu->setTitle("Module update method");
#if defined(BLUETOOTH_USB_UPDATE)
        if (bluetooth.state == BLUETOOTH_STATE_USB_ACTIVE ||
            bluetooth.usbPhase == Bluetooth::USB_RESTARTING ||
            bluetooth.usbPhase == Bluetooth::USB_RESTART_FAILED) {
          menu->addLine("USB update status", [](){ new BluetoothOtaDialog(); });
          return 0;
        }
#endif
        auto start = [](Bluetooth::UpdateMethod method) {
          new ConfirmDialog("Bluetooth update",
            "Stop Bluetooth and update the module?",
            [=]() { bluetooth.requestOta(method); new BluetoothOtaDialog(); });
        };
        if (bluetooth.updateCapabilities & Bluetooth::UPDATE_WIFI)
          menu->addLine("Wi-Fi", [=]() { start(Bluetooth::UPDATE_WIFI); });
        if (bluetooth.updateCapabilities & Bluetooth::UPDATE_BLE)
          menu->addLine("Bluetooth", [=]() { start(Bluetooth::UPDATE_BLE); });
#if defined(BLUETOOTH_USB_UPDATE)
        if (bluetooth.updateCapabilities & Bluetooth::UPDATE_UART)
          menu->addLine("USB", [=]() { start(Bluetooth::UPDATE_UART); });
#endif
        if (!bluetooth.updateCapabilities)
          menu->addLine("No supported update methods", [](){});
        return 0;
      });
  otaBtn->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);

  // BT radio name
  nameEdit = parent->newLine(grid);
  nameEdit->padLeft(PAD_SMALL);
  new StaticText(nameEdit, rect_t{}, STR_NAME);

  box = new Window(nameEdit, rect_t{});
  box->padAll(PAD_TINY);
  box->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_SMALL);
  lv_obj_set_style_grid_cell_x_align(box->getLvObj(), LV_GRID_ALIGN_STRETCH, 0);
  lv_obj_set_style_flex_cross_place(box->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
  new RadioTextEdit(box, rect_t{}, g_eeGeneral.bluetoothName,
                    LEN_BLUETOOTH_NAME);
  nameEdit->show(g_eeGeneral.bluetoothMode != BLUETOOTH_OFF);
}
