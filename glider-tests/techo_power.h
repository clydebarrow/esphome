#pragma once

#include <hal/nrf_power.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

// True while the USB connector supplies power (VBUS present)
inline bool techo_usb_power() { return nrf_power_usbregstatus_vbusdet_get(NRF_POWER); }

// Let only USB power appearing wake the chip from System OFF. Each enabled USB interrupt wakes it, and the USB
// driver also enables USBREMOVED and USBPWRRDY, so unplugging would wake it too.
inline void techo_wake_on_usb() {
  nrf_power_int_disable(NRF_POWER, NRF_POWER_INT_USBREMOVED_MASK | NRF_POWER_INT_USBPWRRDY_MASK);
  nrf_power_event_clear(NRF_POWER, NRF_POWER_EVENT_USBDETECTED);
  nrf_power_event_clear(NRF_POWER, NRF_POWER_EVENT_USBREMOVED);
  nrf_power_event_clear(NRF_POWER, NRF_POWER_EVENT_USBPWRRDY);
  nrf_power_int_enable(NRF_POWER, NRF_POWER_INT_USBDETECTED_MASK);
}

// GPREGRET2 survives System OFF and resets; neither the bootloader nor Zephyr uses it.
static constexpr uint32_t TECHO_OFF_ON_USB = 0xA5;

// Note at power-off whether USB was connected
inline void techo_mark_off() { NRF_POWER->GPREGRET2 = techo_usb_power() ? TECHO_OFF_ON_USB : 0; }

// Unplugging USB from a unit that was powered off on USB starts it: the chip wakes when USB power goes as well as
// when it comes (RESETREAS VBUS), and on some boards the reset pin pulses. True for that start only: the note is
// cleared, so a later press of the reset button starts the unit. The USB voltage may still be decaying, so it waits up
// to 1 s for it to go; if it stays, the reset button was pressed on USB.
inline bool techo_unplug_reset() {
  const uint32_t off_note = NRF_POWER->GPREGRET2;
  const uint32_t reason = NRF_POWER->RESETREAS;
  NRF_POWER->GPREGRET2 = 0;
  // RESETREAS keeps its bits until they are written back
  NRF_POWER->RESETREAS = reason;
  if (off_note != TECHO_OFF_ON_USB || (reason != POWER_RESETREAS_RESETPIN_Msk && reason != POWER_RESETREAS_VBUS_Msk))
    return false;
  for (int ms = 0; ms <= 1000; ms += 10) {
    if (!techo_usb_power())
      return true;
    k_msleep(10);
  }
  return false;
}

// Back to System OFF at once, leaving the display as it is
inline void techo_power_off_now() {
  techo_wake_on_usb();
  sys_poweroff();
}

// Charge left in percent from the battery voltage, on the same Li-ion curve as the live page's (live-page.tsx)
inline int techo_battery_percent(float volts) {
  static const float CURVE[][2] = {{3.3f, 0},   {3.5f, 5},  {3.65f, 20}, {3.75f, 40},
                                   {3.85f, 60}, {4.0f, 85}, {4.2f, 100}};
  if (volts <= CURVE[0][0])
    return 0;
  for (size_t i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); i++) {
    if (volts <= CURVE[i][0]) {
      const float share = (volts - CURVE[i - 1][0]) / (CURVE[i][0] - CURVE[i - 1][0]);
      return static_cast<int>(CURVE[i - 1][1] + share * (CURVE[i][1] - CURVE[i - 1][1]) + 0.5f);
    }
  }
  return 100;
}
