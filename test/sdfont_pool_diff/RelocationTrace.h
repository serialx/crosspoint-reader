#pragma once

// Included inside Trace.cpp's anonymous namespace, after its allocation header.
// Observe the candidate's real slots without adding hooks to the Rust pool.
SdCardFont* observedFont = nullptr;
size_t relocationCount = 0, failMoveAt = 0;
#ifdef SDPOOL_RELOCATION_TRACE
enum class TableKind { Advance, Intervals, Glyphs };
struct Move {
  TableKind kind;
  void* oldData;
  void* newData = nullptr;
  size_t capacity, used;
};
Move moves[3]{};
size_t moveCount = 0, moveNext = 0;
uint8_t moveStyle = 0;
bool awaitingFree = false, awaitingPublish = false;

void requireMove(bool condition) {
  if (!condition) {
    std::fputs("Invalid relocation event, copy or slot publication\n", stderr);
    std::exit(4);
  }
}
const char* tableName(TableKind kind) {
  switch (kind) {
    case TableKind::Advance:
      return "advance";
    case TableKind::Intervals:
      return "intervals";
    case TableKind::Glyphs:
      return "glyphs";
  }
  std::abort();
}
void* tableSlot(TableKind kind) {
  auto& s = observedFont->styleData(moveStyle);
  switch (kind) {
    case TableKind::Advance:
      return observedFont->advanceTable_[moveStyle];
    case TableKind::Intervals:
      return s.miniIntervals;
    case TableKind::Glyphs:
      return s.miniGlyphs;
  }
  std::abort();
}
void checkRelocation() {
  requireMove(!awaitingFree);
  if (awaitingPublish) {
    const auto& m = moves[moveNext - 1];
    requireMove(tableSlot(m.kind) == (m.newData ? m.newData : m.oldData));
    awaitingPublish = false;
  }
}
void resetRelocations() {
  checkRelocation();
  requireMove(moveNext == moveCount);
  moveNext = moveCount = relocationCount = 0;
  observedFont = nullptr;
}
void reportRelocations() {
  requireMove(moveNext == moveCount);
  std::printf("RELOCATIONS requests=%zu failed=%zu\n", relocationCount, failMoveAt ? size_t(1) : size_t(0));
  requireMove(!failMoveAt || relocationCount >= failMoveAt);
}
void expectMove(TableKind kind, void* data, size_t capacity, size_t used) {
  if (!data || !capacity || capacity > 8192) return;
  requireMove(moveCount < 3 && used <= capacity);
  moves[moveCount++] = {kind, data, nullptr, capacity, used};
}
bool relocationRequest(size_t size) {
  checkRelocation();
  if (moveNext == moveCount) return false;
  const auto& m = moves[moveNext];
  requireMove(size == m.capacity && tableSlot(m.kind) == m.oldData);
  ++relocationCount;
  return true;
}
size_t relocationId() { return (static_cast<Allocation*>(moves[moveNext].oldData) - 1)->id; }
bool failRelocation() {
  if (relocationCount != failMoveAt) return false;
  const auto& m = moves[moveNext++];
  std::printf("MOVE %zu style=%u table=%s bytes=%zu copy=%zu failed\n", relocationCount, moveStyle, tableName(m.kind),
              m.capacity, m.used);
  awaitingPublish = true;
  return true;
}
void relocationAllocated(void* p) {
  auto& m = moves[moveNext];
  m.newData = p;
  requireMove(p != m.oldData);
  awaitingFree = true;
}
void checkCopied(const Move& m) {
  // Compare initialized fields only; packed glyph and advance records have padding.
  switch (m.kind) {
    case TableKind::Advance: {
      auto* a = static_cast<SdCardFont::AdvanceEntry*>(m.oldData);
      auto* b = static_cast<SdCardFont::AdvanceEntry*>(m.newData);
      for (size_t i = 0; i < m.used / sizeof(*a); ++i)
        requireMove(a[i].codepoint == b[i].codepoint && a[i].advanceX == b[i].advanceX);
      break;
    }
    case TableKind::Intervals:
      requireMove(std::memcmp(m.oldData, m.newData, m.used) == 0);
      break;
    case TableKind::Glyphs: {
      auto* a = static_cast<EpdGlyph*>(m.oldData);
      auto* b = static_cast<EpdGlyph*>(m.newData);
      for (size_t i = 0; i < m.used / sizeof(*a); ++i)
        requireMove(a[i].width == b[i].width && a[i].height == b[i].height && a[i].advanceX == b[i].advanceX &&
                    a[i].left == b[i].left && a[i].top == b[i].top && a[i].dataLength == b[i].dataLength &&
                    a[i].dataOffset == b[i].dataOffset);
      break;
    }
  }
}
bool relocationFree(void* p) {
  if (awaitingFree) {
    const auto& m = moves[moveNext++];
    requireMove(p == m.oldData);
    checkCopied(m);
    std::printf("MOVE %zu style=%u table=%s bytes=%zu copy=%zu ok\n", relocationCount, moveStyle, tableName(m.kind),
                m.capacity, m.used);
    awaitingFree = false;
    awaitingPublish = true;
    return true;
  }
  checkRelocation();
  requireMove(moveNext == moveCount);
  if (!observedFont || !observedFont->styles_) return false;
  for (uint8_t style = 0; style < 4; ++style) {
    if (!observedFont->hasStyle(style)) continue;
    const auto& s = observedFont->styleData(style);
    if (p != s.miniBitmap || !s.miniGlyphs || !s.miniIntervals) continue;
    size_t total = 0;
    for (size_t i = 0; i < s.miniGlyphCount; ++i) total += s.miniGlyphs[i].dataLength;
    if (total <= s.miniBitmapCapacity) continue;
    moveStyle = style;
    moveNext = moveCount = 0;
    expectMove(TableKind::Advance, observedFont->advanceTable_[style],
               observedFont->advanceTableCapacity_[style] * sizeof(SdCardFont::AdvanceEntry),
               observedFont->advanceTableSize_[style] * sizeof(SdCardFont::AdvanceEntry));
    expectMove(TableKind::Intervals, s.miniIntervals, s.miniIntervalCapacity * sizeof(EpdUnicodeInterval),
               s.miniIntervalCount * sizeof(EpdUnicodeInterval));
    expectMove(TableKind::Glyphs, s.miniGlyphs, s.miniGlyphCapacity * sizeof(EpdGlyph),
               s.miniGlyphCount * sizeof(EpdGlyph));
    break;
  }
  return false;
}
#else
void checkRelocation() {}
void resetRelocations() { relocationCount = 0; }
void reportRelocations() { std::puts("RELOCATIONS requests=0 failed=0"); }
bool relocationRequest(size_t) { return false; }
size_t relocationId() { std::abort(); }
bool failRelocation() { std::abort(); }
void relocationAllocated(void*) { std::abort(); }
bool relocationFree(void*) { return false; }
#endif
