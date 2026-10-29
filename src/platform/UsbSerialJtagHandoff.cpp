#include "UsbSerialJtagHandoff.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <sdkconfig.h>
#endif

#if defined(ARDUINO_ARCH_ESP32) && CONFIG_IDF_TARGET_ESP32S3 && FREEINK_CAP_USB_MSC

#include <Arduino.h>
#include <esp_err.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <hal/clk_gate_ll.h>
#include <hal/usb_serial_jtag_ll.h>
#include <soc/periph_defs.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>
#include <soc/usb_serial_jtag_struct.h>
#include <xp_ota.h>

extern "C" esp_err_t deinit_usb_hal();

namespace {
void IRAM_ATTR onUsbSerialJtagReset(void* context) {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  const uint32_t interruptStatus = usb_serial_jtag_ll_get_intsts_mask();
  usb_serial_jtag_ll_clr_intsts_mask(interruptStatus);
  if (interruptStatus & USB_SERIAL_JTAG_INTR_BUS_RESET) {
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context), &higherPriorityTaskWoken);
  }
  if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

struct Handoff {
  SemaphoreHandle_t semaphore = nullptr;
  intr_handle_t interrupt = nullptr;
};
bool operation(void* opaque, XpUsbOperation op) {
  auto& ctx = *static_cast<Handoff*>(opaque);
  switch (op) {
    case XP_USB_STOP_SERIAL:
      Serial.end();
      break;
    case XP_USB_RESET_PHY: {
      // Match Arduino's usb_switch_to_cdc_jtag(): stop USB-OTG, select the
      // hardware Serial/JTAG PHY, and force a host-visible disconnect/reconnect.
      (void)deinit_usb_hal();
      periph_ll_reset(PERIPH_USB_MODULE);
      periph_ll_disable_clk_set_rst(PERIPH_USB_MODULE);
      CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                          RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
      CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL | USB_SERIAL_JTAG_USB_PAD_ENABLE);

      pinMode(USB_INT_PHY0_DM_GPIO_NUM, OUTPUT_OPEN_DRAIN);
      pinMode(USB_INT_PHY0_DP_GPIO_NUM, OUTPUT_OPEN_DRAIN);
      digitalWrite(USB_INT_PHY0_DM_GPIO_NUM, LOW);
      digitalWrite(USB_INT_PHY0_DP_GPIO_NUM, LOW);

      break;
    }
    case XP_USB_PREPARE_INTERRUPTS: {
      const usb_serial_jtag_pull_override_vals_t pullConfig = {
          .dp_pu = 1,
          .dm_pu = 0,
          .dp_pd = 0,
          .dm_pd = 0,
      };
      usb_serial_jtag_ll_phy_enable_pull_override(&pullConfig);
      usb_serial_jtag_ll_phy_disable_pull_override();
      usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
      usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
      usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);

      break;
    }
    case XP_USB_CREATE_SEMAPHORE:
      ctx.semaphore = xSemaphoreCreateBinary();
      return ctx.semaphore != nullptr;
    case XP_USB_ALLOC_INTERRUPT:
      return esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 0, onUsbSerialJtagReset, ctx.semaphore, &ctx.interrupt) ==
             ESP_OK;
    case XP_USB_DELETE_SEMAPHORE:
      vSemaphoreDelete(ctx.semaphore);
      ctx.semaphore = nullptr;
      break;
    case XP_USB_CONNECT:
      SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
      break;
    case XP_USB_WAIT_RESET:
      (void)xSemaphoreTake(ctx.semaphore, pdMS_TO_TICKS(1000));
      break;
    case XP_USB_DISABLE_INTERRUPTS:
      usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
      break;
    case XP_USB_FREE_INTERRUPT:
      esp_intr_free(ctx.interrupt);
      break;
    case XP_USB_DELAY:
      delay(20);
      break;
  }
  return true;
}
}  // namespace
void handoffUsbOtgToSerialJtag() {
  Handoff ctx;
  xp_ota_usb_handoff(&ctx, operation);
}

#else

void handoffUsbOtgToSerialJtag() {}

#endif
