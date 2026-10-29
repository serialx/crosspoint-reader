#pragma once
#include "Oracle.h"
using esp_err_t = int;
constexpr int ESP_OK=0;
constexpr unsigned ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_OTA=0, ESP_PARTITION_SUBTYPE_APP_OTA_0=16;
struct esp_partition_t { uint32_t size=131072; const char* label="ota_1"; unsigned address=0x200000; uint8_t subtype=17; };
inline esp_partition_t oraclePartition, oracleData{8192,"otadata",0xe000,0};
inline const char* esp_err_to_name(int e) { return e ? "FAKE_ERROR" : "ESP_OK"; }
inline const esp_partition_t* esp_partition_find_first(unsigned t,unsigned s,const char*) { oracle::record("find %u %u",t,s); oracle::boundary(); return oracle::fault=="find" ? nullptr : &oracleData; }
inline int esp_partition_read(const esp_partition_t*,size_t off,void* out,size_t n) { oracle::record("read %zu %zu",off,n); oracle::boundary(); if(oracle::fault=="read" || (oracle::fault=="read-second" && off)) return -1; memcpy(out,oracle::otadata.data()+off,n); return 0; }
inline int esp_partition_erase_range(const esp_partition_t*,size_t off,size_t n) { oracle::record("erase %zu %zu",off,n); oracle::boundary(); if(oracle::fault=="erase") return -1; memset(oracle::otadata.data()+off,255,n); oracle::boundary(); return 0; }
inline int esp_partition_write(const esp_partition_t*,size_t off,const void* p,size_t n) { oracle::record("partition-write %zu %zu %u",off,n,oracle::hash(static_cast<const uint8_t*>(p),n)); oracle::boundary(); if(oracle::fault=="partition-write") return -1; memcpy(oracle::otadata.data()+off,p,n); oracle::boundary(); return 0; }
