#pragma once

class GfxRenderer;

typedef enum { TEXT_BLOCK, IMAGE_BLOCK } BlockType;

// a block of content in the html - either a paragraph or an image
class Block {
 public:
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  virtual ~Block() {
    extern void chapterTestBlockDestroyed(const void*);
    chapterTestBlockDestroyed(this);
  }
#else
  virtual ~Block() = default;
#endif

  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
  virtual void finish() {}
};
