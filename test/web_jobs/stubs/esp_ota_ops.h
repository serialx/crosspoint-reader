#pragma once
#include "esp_partition.h"
inline const esp_partition_t* esp_ota_get_running_partition() { return &oraclePartition; }
inline const esp_partition_t* esp_ota_get_next_update_partition(void*) { oracle::record("partition"); return oracle::fault=="partition" ? nullptr : &oraclePartition; }
