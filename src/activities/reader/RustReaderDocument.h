#pragma once
#include <cstdint>

// Synchronous HAL/MD5 operations in 32 pointer-aligned caller words; no callbacks.
// Operation ordinals and the 32-word capacity must match book_owner/document.rs.
// Storage stays pinned from Init through Drop, including failed Open; paths are NUL-terminated.
int64_t readerDocumentIo(void* storage, uint8_t operation, const uint8_t* input, uint8_t* output, uint32_t value);
