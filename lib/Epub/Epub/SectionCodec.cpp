#include "SectionCodec.h"

#include <BufferedFile.h>
#include <Logging.h>

#include <cstring>

namespace sectioncodec {
XpSecRecord record(const XpSecKind kind, std::initializer_list<uint32_t> values) {
  XpSecRecord result{};
  result.kind = kind;
  size_t index = 0;
  for (const uint32_t value : values) {
    if (index == 20) break;
    result.values[index++] = value;
  }
  return result;
}

bool write(HalFile& file, const XpSecRecord& record) {
  uint8_t bytes[XP_SEC_HEADER_SIZE];
  size_t required = 0;
  if (xp_sec_encode_record(&record, bytes, sizeof(bytes), &required) != XP_SEC_OK ||
      file.write(bytes, required) != required) {
    LOG_ERR("SCT", "Failed to encode/write cache record %u", record.kind);
    return false;
  }
  return true;
}

bool read(HalFile& file, const XpSecKind kind, XpSecRecord& record, uint32_t* rejectedVersion) {
  uint8_t bytes[XP_SEC_HEADER_SIZE];
  const size_t required = xp_sec_record_size(kind);
  if (required == 0 || required > sizeof(bytes) || file.read(bytes, required) != required) {
    LOG_ERR("SCT", "Short cache record %u", static_cast<unsigned>(kind));
    return false;
  }
  XpSecDecoder decoder{bytes, required, 0, static_cast<uint32_t>(kind)};
  if (xp_sec_decode_next(&decoder, &record) != XP_SEC_OK) {
    if (rejectedVersion && kind == XP_SEC_HEADER) {
      *rejectedVersion = bytes[0];
    } else {
      LOG_ERR("SCT", "Invalid cache record %u", static_cast<unsigned>(kind));
    }
    return false;
  }
  return true;
}

bool writeValue(HalFile& file, const XpSecKind kind, const uint32_t value) {
  return write(file, record(kind, {value}));
}

bool readValue(HalFile& file, const XpSecKind kind, uint32_t& value) {
  XpSecRecord decoded;
  if (!read(file, kind, decoded)) return false;
  value = decoded.values[0];
  return true;
}

bool writeString(HalFile& file, const std::string& value) {
  return writeValue(file, XP_SEC_U32, value.size()) && file.write(value.data(), value.size()) == value.size();
}

bool readString(HalFile& file, std::string& value) {
  uint32_t length = 0;
  if (!readValue(file, XP_SEC_U32, length)) return false;
  uint32_t position = 0;
  if (!xp_sec_table_entry(file.position(), 0, length, file.size(), 0, &position)) {
    LOG_ERR("SCT", "Invalid cache string length %u", length);
    return false;
  }
  value.resize(length);
  return length == 0 || file.read(value.data(), length) == length;
}

bool readAnchor(serialization::BufferedFileReader& file, const uint32_t fileSize, std::string& anchor, uint16_t& page) {
  uint8_t bytes[4];
  XpSecRecord record;
  XpSecDecoder decoder{bytes, sizeof(bytes), 0, XP_SEC_U32};
  uint32_t position;
  if (file.read(bytes, 4) != 4 || xp_sec_decode_next(&decoder, &record) != XP_SEC_OK ||
      !xp_sec_table_entry(file.position(), 0, record.values[0], fileSize, 0, &position))
    return false;
  anchor.resize(record.values[0]);
  if (file.read(anchor.data(), anchor.size()) != anchor.size() || file.read(bytes, 2) != 2) return false;
  decoder = {bytes, 2, 0, XP_SEC_U16};
  if (xp_sec_decode_next(&decoder, &record) != XP_SEC_OK) return false;
  page = record.values[0];
  return true;
}

XpSecRecord header(const ReaderRenderSpec& spec, const uint16_t pageCount) {
  uint32_t compression;
  memcpy(&compression, &spec.lineCompression, sizeof(compression));
  return record(XP_SEC_HEADER, {0,
                                static_cast<uint32_t>(spec.fontId),
                                compression,
                                spec.extraParagraphSpacing,
                                spec.paragraphIndentSpaces,
                                spec.paragraphAlignment,
                                spec.viewportWidth,
                                spec.viewportHeight,
                                spec.hyphenationEnabled,
                                spec.embeddedStyle,
                                spec.imageRendering,
                                spec.focusReadingEnabled,
                                static_cast<uint8_t>(spec.characterSpacing),
                                spec.wordSpacingPercent,
                                pageCount,
                                0,
                                0,
                                0,
                                0,
                                0});
}
}  // namespace sectioncodec
