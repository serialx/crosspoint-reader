/**
 * XtcParser.cpp
 *
 * XTC file parsing implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "XtcParser.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace xtc {

XtcParser::XtcParser()
    : m_isOpen(false),
      m_defaultWidth(DISPLAY_WIDTH),
      m_defaultHeight(DISPLAY_HEIGHT),
      m_bitDepth(1),
      m_hasChapters(false),
      m_chaptersLoaded(false),
      m_lastError(XtcError::OK) {
  memset(&m_header, 0, sizeof(m_header));
}

XtcParser::~XtcParser() { close(); }

XtcError XtcParser::open(const char* filepath) {
  // Close if already open
  if (m_isOpen) {
    close();
  }

  m_filepath = filepath;

  // Open file
  if (!Storage.openFileForRead("XTC", filepath, m_file)) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return m_lastError;
  }

  // Read header
  m_lastError = readHeader();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read header: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Read title & author if available
  if (m_header.hasMetadata) {
    m_lastError = readTitle();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read title: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    m_lastError = readAuthor();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read author: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    // Trim excess capacity from metadata strings
    m_title.shrink_to_fit();
    m_author.shrink_to_fit();
  }

  // Read first page info for default dimensions (no bulk page table allocation)
  m_lastError = readFirstPageInfo();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read first page info: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Defer chapter parsing until actually needed (lazy load).
  // Chapter strings can use significant heap; keeping them out of memory
  // during rendering leaves more room for the page bitmap buffer.
  // Older XTC files start the page table at 0x30, so they do not have the later
  // chapterOffset field even if the bytes read into that slot are non-zero.
  m_hasChapters = xp_xtc_has_chapters(&m_header);
  m_chaptersLoaded = false;

  // Close the source file to free its internal SdFat buffers.
  // It will be reopened on-demand for page table lookups and bitmap reads.
  m_file.close();

  m_isOpen = true;
  LOG_DBG("XTC", "Opened file: %s (%u pages, %dx%d)", filepath, m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

void XtcParser::close() {
  closeFile();
  m_isOpen = false;
  m_chaptersLoaded = false;
  m_chapters.clear();
  m_title.clear();
  m_author.clear();
  m_hasChapters = false;
  memset(&m_header, 0, sizeof(m_header));
}

bool XtcParser::ensureFileOpen() {
  if (m_file.isOpen()) {
    return true;
  }
  return Storage.openFileForRead("XTC", m_filepath.c_str(), m_file);
}

void XtcParser::closeFile() {
  if (m_file.isOpen()) {
    m_file.close();
  }
}

XtcError XtcParser::readHeader() {
  uint8_t bytes[sizeof(XtcHeader)];
  const size_t count = m_file.read(bytes, sizeof(bytes));
  // Preserve the public header snapshot after a short or invalid header read.
  if (count <= sizeof(bytes)) {
    memcpy(&m_header, bytes, count);
  }
  if (count == sizeof(bytes)) {
    const uint8_t depth = xp_xtc_bit_depth(bytes, count);
    if (depth != 0) {
      m_bitDepth = depth;
    }
  }
  return static_cast<XtcError>(xp_xtc_header(bytes, count, &m_header));
}

XtcError XtcParser::readTitle() {
  if (!m_file.seek(xp_xtc_metadata_offset(false))) {
    return XtcError::READ_ERROR;
  }

  char titleBuf[128] = {0};
  m_file.read(titleBuf, sizeof(titleBuf) - 1);
  m_title.assign(titleBuf, xp_xtc_text_len(reinterpret_cast<const uint8_t*>(titleBuf), sizeof(titleBuf) - 1));

  LOG_DBG("XTC", "Title: %s", m_title.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readAuthor() {
  // Read author as null-terminated UTF-8 string with max length 64, directly following title
  if (!m_file.seek(xp_xtc_metadata_offset(true))) {
    return XtcError::READ_ERROR;
  }

  char authorBuf[64] = {0};
  m_file.read(authorBuf, sizeof(authorBuf) - 1);
  m_author.assign(authorBuf, xp_xtc_text_len(reinterpret_cast<const uint8_t*>(authorBuf), sizeof(authorBuf) - 1));

  LOG_DBG("XTC", "Author: %s", m_author.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readFirstPageInfo() {
  if (!xp_xtc_table_valid(&m_header, m_file.fileSize64())) {
    return XtcError::CORRUPTED_HEADER;
  }
  if (!m_file.seek64(m_header.pageTableOffset)) {
    return XtcError::READ_ERROR;
  }
  uint8_t bytes[16];
  XpXtcEntry entry;
  const auto error = static_cast<XtcError>(xp_xtc_entry(bytes, m_file.read(bytes, sizeof(bytes)), &entry));
  if (error != XtcError::OK) {
    return error;
  }

  m_defaultWidth = entry.width;
  m_defaultHeight = entry.height;

  LOG_DBG("XTC", "Page table validated: %u pages, default %dx%d", m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

bool XtcParser::readPageTableEntry(uint32_t pageIndex, PageInfo& info) {
  if (pageIndex >= m_header.pageCount) {
    return false;
  }

  if (!ensureFileOpen()) {
    LOG_DBG("XTC", "Failed to reopen file for page table read");
    return false;
  }

  // Seek to the specific page table entry on the SD card
  const uint64_t entryOffset = m_header.pageTableOffset + static_cast<uint64_t>(pageIndex) * sizeof(PageTableEntry);
  if (!m_file.seek64(entryOffset)) {
    LOG_DBG("XTC", "Failed to seek to page table entry %lu at %llu", pageIndex, entryOffset);
    return false;
  }

  uint8_t bytes[16];
  XpXtcEntry entry;
  if (xp_xtc_entry(bytes, m_file.read(bytes, sizeof(bytes)), &entry) != 0) {
    return false;
  }
  info.offset = entry.offset;
  info.size = entry.size;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = m_bitDepth;
  return true;
}

XtcError XtcParser::readChapters() {
  m_chapters.clear();

  if (!ensureFileOpen()) {
    return XtcError::READ_ERROR;
  }

  uint8_t hasChaptersFlag = 0;
  if (!m_file.seek(0x0B)) {
    return XtcError::READ_ERROR;
  }
  if (m_file.read(&hasChaptersFlag, sizeof(hasChaptersFlag)) != sizeof(hasChaptersFlag)) {
    return XtcError::READ_ERROR;
  }

  if (!xp_xtc_chapter_flag(hasChaptersFlag)) {
    return XtcError::OK;
  }

  uint8_t offsetBytes[8];
  if (!m_file.seek(0x30) || m_file.read(offsetBytes, sizeof(offsetBytes)) != sizeof(offsetBytes)) {
    return XtcError::READ_ERROR;
  }
  uint64_t chapterOffset = 0;
  uint64_t count = 0;
  xp_xtc_chapter_span(&m_header, offsetBytes, sizeof(offsetBytes), m_file.fileSize64(), &chapterOffset, &count);
  const size_t chapterCount = static_cast<size_t>(count);
  if (chapterCount == 0) {
    return XtcError::OK;
  }
  if (!m_file.seek64(chapterOffset)) {
    return XtcError::READ_ERROR;
  }
  m_chapters.reserve(chapterCount);
  uint8_t chapterBuf[96];
  bool stop = false;
  for (size_t i = 0; i < chapterCount && !stop; i++) {
    XpXtcChapter chapter;
    const auto error = static_cast<XtcError>(
        xp_xtc_chapter(chapterBuf, m_file.read(chapterBuf, sizeof(chapterBuf)), m_header.pageCount, &chapter));
    if (error != XtcError::OK) {
      return error;
    }
    switch (chapter.action) {
      case XpXtcChapterAction::Skip:
        break;
      case XpXtcChapterAction::Emit:
        m_chapters.push_back(
            {std::string(reinterpret_cast<const char*>(chapterBuf), chapter.name_len), chapter.start, chapter.end});
        break;
      case XpXtcChapterAction::Stop:
        stop = true;
        break;
    }
  }

  m_hasChapters = !m_chapters.empty();
  LOG_DBG("XTC", "Chapters: %u", static_cast<unsigned int>(m_chapters.size()));
  return XtcError::OK;
}

const std::vector<ChapterInfo>& XtcParser::getChapters() {
  // Lazy load chapters on first access
  if (!m_chaptersLoaded && m_hasChapters) {
    const XtcError err = readChapters();
    if (err != XtcError::OK) {
      LOG_ERR("XTC", "Failed to lazy-load chapters: %s", errorToString(err));
      m_hasChapters = false;
      m_chapters.clear();
    }
    m_chaptersLoaded = true;
    // Close file after chapter read to free buffers for rendering
    closeFile();
  }
  return m_chapters;
}

bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) { return readPageTableEntry(pageIndex, info); }

size_t XtcParser::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  if (!m_isOpen) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }

  if (pageIndex >= m_header.pageCount) {
    m_lastError = XtcError::PAGE_OUT_OF_RANGE;
    return 0;
  }

  PageInfo page;
  if (!readPageTableEntry(pageIndex, page)) {
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  if (!ensureFileOpen()) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }

  // Seek to page data
  if (!m_file.seek64(page.offset)) {
    LOG_DBG("XTC", "Failed to seek to page %u at offset %llu", pageIndex, static_cast<unsigned long long>(page.offset));
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  uint8_t pageHeader[22];
  uint32_t bitmapSize = 0;
  m_lastError = static_cast<XtcError>(
      xp_xtc_page(pageHeader, m_file.read(pageHeader, sizeof(pageHeader)), m_bitDepth, false, &bitmapSize));
  if (m_lastError != XtcError::OK) {
    return 0;
  }

  // Check buffer size
  if (bufferSize < bitmapSize) {
    LOG_DBG("XTC", "Buffer too small: need %u, have %u", bitmapSize, bufferSize);
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }

  // Read bitmap data
  size_t bytesRead = m_file.read(buffer, bitmapSize);
  if (bytesRead != bitmapSize) {
    LOG_DBG("XTC", "Page read error: expected %u, got %u", bitmapSize, bytesRead);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  m_lastError = XtcError::OK;
  return bytesRead;
}

XtcError XtcParser::loadPageStreaming(uint32_t pageIndex,
                                      std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                      size_t chunkSize) {
  if (!m_isOpen) {
    return XtcError::FILE_NOT_FOUND;
  }

  if (pageIndex >= m_header.pageCount) {
    return XtcError::PAGE_OUT_OF_RANGE;
  }

  PageInfo page;
  if (!readPageTableEntry(pageIndex, page)) {
    return XtcError::READ_ERROR;
  }

  if (!ensureFileOpen()) {
    return XtcError::FILE_NOT_FOUND;
  }

  // Seek to page data
  if (!m_file.seek64(page.offset)) {
    return XtcError::READ_ERROR;
  }

  uint8_t pageHeader[22];
  uint32_t bitmapSize = 0;
  const auto error = static_cast<XtcError>(
      xp_xtc_page(pageHeader, m_file.read(pageHeader, sizeof(pageHeader)), m_bitDepth, true, &bitmapSize));
  if (error != XtcError::OK) {
    return error;
  }

  // Read in chunks
  std::vector<uint8_t> chunk(chunkSize);
  size_t totalRead = 0;

  while (totalRead < bitmapSize) {
    size_t toRead = std::min(chunkSize, static_cast<size_t>(bitmapSize) - totalRead);
    size_t bytesRead = m_file.read(chunk.data(), toRead);

    if (bytesRead == 0) {
      return XtcError::READ_ERROR;
    }

    callback(chunk.data(), bytesRead, totalRead);
    totalRead += bytesRead;
  }

  return XtcError::OK;
}

bool XtcParser::isValidXtcFile(const char* filepath) {
  HalFile file;
  if (!Storage.openFileForRead("XTC", filepath, file)) {
    return false;
  }

  uint8_t magic[4];
  const size_t count = file.read(magic, sizeof(magic));
  file.close();
  return xp_xtc_bit_depth(magic, count) != 0;
}

}  // namespace xtc
