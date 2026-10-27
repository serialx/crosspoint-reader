#include <BidiUtils.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/TokenBoundary.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>

bool isExplicitHyphen(uint32_t) { return false; }
bool isSoftHyphen(uint32_t) { return false; }

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : ownedImagePath(imagePath),
      ownedSrcPath(srcPath),
      imagePath(ownedImagePath),
      srcPath(ownedSrcPath),
      width(width),
      height(height) {}

bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  return Epub::enableGifProbeForTest && path.ends_with(".gif");
}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }

void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::needsDecode() const { return false; }
