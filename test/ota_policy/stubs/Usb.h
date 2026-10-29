#pragma once
#include "Oracle.h"
#define IRAM_ATTR
using esp_err_t=int;
using BaseType_t=int;
using SemaphoreHandle_t=void*;
using intr_handle_t=void*;
constexpr int ESP_OK=0,pdFALSE=0,pdTRUE=1;
constexpr unsigned USB_SERIAL_JTAG_INTR_BUS_RESET=1,USB_SERIAL_JTAG_LL_INTR_MASK=255;
constexpr unsigned PERIPH_USB_MODULE=1,RTC_CNTL_USB_CONF_REG=2,RTC_CNTL_SW_HW_USB_PHY_SEL=1,RTC_CNTL_SW_USB_PHY_SEL=2,RTC_CNTL_USB_PAD_ENABLE=4,USB_SERIAL_JTAG_CONF0_REG=3,USB_SERIAL_JTAG_PHY_SEL=8,USB_SERIAL_JTAG_USB_PAD_ENABLE=16;
constexpr int USB_INT_PHY0_DM_GPIO_NUM=19,USB_INT_PHY0_DP_GPIO_NUM=20,OUTPUT_OPEN_DRAIN=3,LOW=0,ETS_USB_SERIAL_JTAG_INTR_SOURCE=42;
struct usb_serial_jtag_pull_override_vals_t { int dp_pu,dm_pu,dp_pd,dm_pd; };
inline void (*oracleIrq)(void*); inline void* oracleIrqCtx;
struct SerialType { void end(){oracle::record("serial-end");} }; inline SerialType Serial;
extern "C" inline esp_err_t deinit_usb_hal(){oracle::record("usb-deinit");return 0;}
inline void periph_ll_reset(unsigned x){oracle::record("reset %u",x);}
inline void periph_ll_disable_clk_set_rst(unsigned x){oracle::record("clock-disable %u",x);}
inline void CLEAR_PERI_REG_MASK(unsigned a,unsigned b){oracle::record("reg-clear %u %u",a,b);}
inline void SET_PERI_REG_MASK(unsigned a,unsigned b){oracle::record("reg-set %u %u",a,b);}
inline void pinMode(int a,int b){oracle::record("pin-mode %d %d",a,b);}
inline void digitalWrite(int a,int b){oracle::record("pin-write %d %d",a,b);}
inline void usb_serial_jtag_ll_phy_enable_pull_override(const usb_serial_jtag_pull_override_vals_t* p){oracle::record("pull %d %d %d %d",p->dp_pu,p->dm_pu,p->dp_pd,p->dm_pd);}
inline void usb_serial_jtag_ll_phy_disable_pull_override(){oracle::record("pull-disable");}
inline void usb_serial_jtag_ll_disable_intr_mask(unsigned a){oracle::record("irq-disable %u",a);}
inline void usb_serial_jtag_ll_clr_intsts_mask(unsigned a){oracle::record("irq-clear %u",a);}
inline void usb_serial_jtag_ll_ena_intr_mask(unsigned a){oracle::record("irq-enable %u",a);}
inline unsigned usb_serial_jtag_ll_get_intsts_mask(){oracle::record("irq-status");return 1;}
inline void xSemaphoreGiveFromISR(void*,BaseType_t* w){oracle::record("give-from-isr");*w=1;}
inline void portYIELD_FROM_ISR(){oracle::record("yield-isr");}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){oracle::record("semaphore-create");return oracle::fault=="semaphore" ? nullptr : reinterpret_cast<void*>(1);}
inline int esp_intr_alloc(int a,int b,void(*isr)(void*),void* ctx,intr_handle_t* handle){oracle::record("irq-alloc %d %d",a,b);oracleIrq=isr;oracleIrqCtx=ctx;*handle=reinterpret_cast<void*>(2);return oracle::fault=="interrupt" ? -1 : 0;}
inline void vSemaphoreDelete(void*){oracle::record("semaphore-delete");}
inline unsigned pdMS_TO_TICKS(unsigned n){return n;}
inline int xSemaphoreTake(void*,unsigned ticks){oracle::record("semaphore-take %u",ticks);if(oracle::fault!="timeout")oracleIrq(oracleIrqCtx);return 1;}
inline void esp_intr_free(void*){oracle::record("irq-free");}
inline void delay(unsigned ms){oracle::record("delay %u",ms);}
