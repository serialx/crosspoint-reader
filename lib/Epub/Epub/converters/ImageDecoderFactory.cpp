#include "ImageDecoderFactory.h"

#include <Logging.h>
#include <Memory.h>
#include <xp_book.h>

#include <memory>
#include <string>

#include "GifToFramebufferConverter.h"
#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;
std::unique_ptr<GifToFramebufferConverter> ImageDecoderFactory::gifDecoder = nullptr;

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  std::string ext = imagePath;
  const XpBookExtensionServices services{&ext,
                                         [](void* raw) -> XpBookMutableSpan {
                                           auto& text = *static_cast<std::string*>(raw);
                                           return {reinterpret_cast<uint8_t*>(text.data()), text.size()};
                                         },
                                         [](void* raw, size_t offset) {
                                           auto& text = *static_cast<std::string*>(raw);
                                           if (offset == SIZE_MAX)
                                             text = "";
                                           else
                                             text = text.substr(offset);
                                         }};
  const auto kind = xp_book_image_decoder(&services);
  if (kind == 1) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new JpegToFramebufferConverter());
    }
    return jpegDecoder.get();
  } else if (kind == 2) {
    if (!pngDecoder) {
      pngDecoder.reset(new PngToFramebufferConverter());
    }
    return pngDecoder.get();
  } else if (kind == 3) {
    if (!gifDecoder) {
      gifDecoder = makeUniqueNoThrow<GifToFramebufferConverter>();
      if (!gifDecoder) {
        LOG_ERR("DEC", "OOM: GIF decoder");
        return nullptr;
      }
    }
    return gifDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
