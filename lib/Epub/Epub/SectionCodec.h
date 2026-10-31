#pragma once

#include <HalStorage.h>
#include <xp_ffi.h>

#include <initializer_list>
#include <string>

#include "ReaderRenderSpec.h"

namespace serialization {
class BufferedFileReader;
}

// HalFile and object storage stay in C++; Rust owns field widths and validation.
namespace sectioncodec {
XpSecRecord record(XpSecKind kind, std::initializer_list<uint32_t> values);
bool write(HalFile& file, const XpSecRecord& record);
// For a rejected header version, return its byte instead of logging the generic record error.
bool read(HalFile& file, XpSecKind kind, XpSecRecord& record, uint32_t* rejectedVersion = nullptr);
bool writeValue(HalFile& file, XpSecKind kind, uint32_t value);
bool readValue(HalFile& file, XpSecKind kind, uint32_t& value);
bool writeString(HalFile& file, const std::string& value);
bool readString(HalFile& file, std::string& value);
bool readAnchor(serialization::BufferedFileReader& file, uint32_t fileSize, std::string& anchor, uint16_t& page);
XpSecRecord header(const ReaderRenderSpec& spec, uint16_t pageCount);
}  // namespace sectioncodec
