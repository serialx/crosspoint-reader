#pragma once

#include <HalStorage.h>
#include <Logging.h>
#include <xp_ui_reader.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ProgressFile {

// The temporary file is closed before remove/rename. A failed write leaves the
// old progress intact; FAT remove+rename can still lose it during power failure.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = cachePath + "/progress.bin.tmp";
  struct Context {
    const std::string& finalPath;
    const std::string& tmpPath;
    std::optional<HalFile> file{std::in_place};
  } context{finalPath, tmpPath};
  const XpUiReaderProgressIo io{&context,
                                [](void* p) -> uint8_t {
                                  auto& c = *static_cast<Context*>(p);
                                  return Storage.openFileForWrite("PRG", c.tmpPath, *c.file);
                                },
                                [](void* p, const uint8_t* bytes, uint32_t size) -> uint32_t {
                                  return static_cast<Context*>(p)->file->write(bytes, size);
                                },
                                [](void* p) { static_cast<Context*>(p)->file->flush(); },
                                [](void* p) { static_cast<Context*>(p)->file.reset(); },
                                [](void* p) { Storage.remove(static_cast<Context*>(p)->finalPath.c_str()); },
                                [](void* p) -> uint8_t {
                                  const auto& c = *static_cast<Context*>(p);
                                  return Storage.rename(c.tmpPath.c_str(), c.finalPath.c_str());
                                }};
  uint32_t written = 0;
  switch (static_cast<XpUiReaderProgressStatus>(xp_ui_reader_write_progress(&io, data, len, &written))) {
    case XP_UI_READER_PROGRESS_OK:
      return true;
    case XP_UI_READER_PROGRESS_OPEN_FAILED:
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    case XP_UI_READER_PROGRESS_SHORT_WRITE:
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    case XP_UI_READER_PROGRESS_RENAME_FAILED:
      LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
      return false;
    case XP_UI_READER_PROGRESS_INVALID:
      LOG_ERR("PRG", "Invalid progress write");
      return false;
  }
  return false;
}
}  // namespace ProgressFile
