#pragma once
#include <cstddef>
#include <cstdint>
inline uintptr_t stackBase = 0x10000;
inline size_t stackFree = 16000;
inline bool globalInterruptsEnabled = true;
inline void* esp_cpu_get_sp() { return reinterpret_cast<void*>(stackBase + stackFree); }
#define CONFIG_IDF_TARGET_ARCH_RISCV 1
#define MSTATUS_MIE 8
#define RV_READ_CSR(reg) (globalInterruptsEnabled ? MSTATUS_MIE : 0)
