#pragma once
inline bool esp_stack_ptr_is_sane(uint32_t) { return false; }
inline bool esp_ptr_in_dram(const void*) { return false; }
