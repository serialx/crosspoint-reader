/**
 * XtcParser.h
 *
 * XTC file parsing and page data extraction
 * XTC ebook support for CrossPoint Reader
 */

#pragma once

#include <HalStorage.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "XtcTypes.h"

namespace xtc {

/**
 * XTC File Parser
 *
 * Reads XTC files from SD card and extracts page data.
 * Designed for ESP32-C3's limited RAM (~380KB) using streaming.
 *
 * The source file is kept closed between reads to free heap for rendering.
 * It is reopened on-demand for page table lookups and bitmap data reads.
 */
class XtcParser {
 public:
  XtcParser();
  ~XtcParser();

  // File open/close
  XtcError open(const char* filepath);
  void close();
  bool isOpen() const { return m_state.opened; }

  // Header information access
  const XtcHeader& getHeader() const { return m_state.header; }
  uint16_t getPageCount() const { return m_state.header.pageCount; }
  uint16_t getWidth() const { return m_state.width; }
  uint16_t getHeight() const { return m_state.height; }
  uint8_t getBitDepth() const { return m_state.depth; }  // 1 = XTC/XTG, 2 = XTCH/XTH

  // Page information
  bool getPageInfo(uint32_t pageIndex, PageInfo& info);

  /**
   * Load page bitmap (raw 1-bit data, skipping XTG header)
   *
   * @param pageIndex Page index (0-based)
   * @param buffer Output buffer (caller allocated)
   * @param bufferSize Buffer size
   * @return Number of bytes read on success, 0 on failure
   */
  size_t loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize);

  /**
   * Streaming page load
   * Memory-efficient method that reads page data in chunks.
   *
   * @param pageIndex Page index
   * @param callback Callback function to receive data chunks
   * @param chunkSize Chunk size (default: 1024 bytes)
   * @return Error code
   */
  XtcError loadPageStreaming(uint32_t pageIndex,
                             std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                             size_t chunkSize = 1024);

  // Get title/author from metadata
  const std::string& getTitle() const { return m_title; }
  const std::string& getAuthor() const { return m_author; }

  bool hasChapters() const { return m_state.has_chapters; }
  const std::vector<ChapterInfo>& getChapters();

  // Validation
  static bool isValidXtcFile(const char* filepath);

  // Error information
  XtcError getLastError() const { return static_cast<XtcError>(m_state.error); }

 private:
  HalFile m_file;
  std::string m_filepath;
  XpBookXtcState m_state{};
  std::vector<ChapterInfo> m_chapters;
  std::string m_title;
  std::string m_author;
  XpBookXtcServices services();
  void diagnostic(uint32_t event, uint32_t a, uint32_t b, uint32_t c);
};

}  // namespace xtc
