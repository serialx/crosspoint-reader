#pragma once
#include "esp_partition.h"
using esp_ota_handle_t = uint32_t;
constexpr size_t OTA_SIZE_UNKNOWN = SIZE_MAX;
inline const esp_partition_t* esp_ota_get_next_update_partition(void*) { oracle::record("partition"); oracle::boundary(); return oracle::fault=="partition" ? nullptr : &oraclePartition; }
inline int esp_ota_begin(const esp_partition_t*,size_t n,esp_ota_handle_t* h) { oracle::record("begin %s",n==OTA_SIZE_UNKNOWN ? "unknown" : "size"); oracle::boundary(); *h=7; return oracle::fault=="begin" ? -1 : 0; }
inline int esp_ota_write(esp_ota_handle_t h,const void* p,size_t n) { oracle::record("write %u %zu %u",h,n,oracle::hash(static_cast<const uint8_t*>(p),n)); oracle::boundary(); if(oracle::fault=="write") return -1; auto b=static_cast<const uint8_t*>(p); oracle::written.insert(oracle::written.end(),b,b+n); oracle::boundary(); return 0; }
inline int esp_ota_abort(esp_ota_handle_t h) { oracle::record("abort %u",h); oracle::boundary(); return 0; }
inline int esp_ota_end(esp_ota_handle_t h) { oracle::record("end %u",h); oracle::boundary(); if(oracle::fault=="verify" || oracle::written.size()!=96) return -1; oracle::verified=true; oracle::boundary(); return 0; }
inline int esp_ota_set_boot_partition(const esp_partition_t*) { oracle::record("select"); oracle::boundary(); if(!oracle::verified) throw std::logic_error("unverified boot"); if(oracle::fault=="select") return -1; oracle::selected=true; oracle::boundary(); return 0; }
