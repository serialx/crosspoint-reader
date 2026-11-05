#include <Epub/PageLink.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/VisibleTextUtils.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/htmlEntities.h>

#include <cstdio>
#include <iostream>
#include <string>

int main() {
  ReaderRenderSpec spec;
  for (int i = 0; i < 512; ++i) {
    spec.fontId = i - 256;
    spec.lineCompression = float(i % 5 + 1) / 4;
    spec.extraParagraphSpacing = i & 1;
    spec.paragraphIndentSpaces = i % 8;
    spec.characterSpacing = i % 9 - 4;
    spec.wordSpacingPercent = i % 256;
    spec.paragraphAlignment = i % 5;
    spec.viewportWidth = i * 2;
    spec.viewportHeight = i * 3;
    spec.hyphenationEnabled = i & 2;
    spec.embeddedStyle = i & 4;
    spec.imageRendering = i % 3;
    spec.focusReadingEnabled = i & 8;
    printf("signature %d %u\n", i, readerRenderSpecSignature(spec));
  }
  PageLink link;
  link.x = -10;
  link.y = 17;
  for (int width : {-32768, -3, 0, 1, 11, 32767}) {
    link.width = width;
    link.height = width;
    for (int x : {-32768, -20, -10, -9, 0, 10, 17, 18, 28, 32767})
      for (int y : {-20, 0, 16, 17, 18, 28, 32767})
        for (int slop : {-2, 0, 5})
          for (int minimum : {0, 15, 50})
            printf("hit %d %d %d %d %d %d\n", width, x, y, slop, minimum, link.contains(x, y, slop, minimum));
  }
  for (int64_t width : {-1LL, 0LL, 1LL, 2048LL, 4096LL, 32767LL, 32768LL, 4294967296LL})
    for (int64_t height : {-1LL, 0LL, 1LL, 2048LL, 4096LL, 32767LL, 32768LL, 4294967296LL}) {
      ImageDimensions dims{-7, -9};
      const bool ok = ImageToFramebufferDecoder::validateAndStoreDimensions(width, height, dims, "TEST");
      printf("dimensions %lld %lld %d %d %d\n", static_cast<long long>(width), static_cast<long long>(height), ok,
             dims.width, dims.height);
    }
  for (const char* name : {"head", "HeAd", "style", "STYLE", "script", "title", "rp", "RP", "body", "headx", ""})
    printf("visible %s %d\n", name, VisibleTextUtils::isNonVisibleElement(name));
  std::string entity;
  while (std::getline(std::cin, entity)) {
    const char* value = lookupHtmlEntity(entity.data(), entity.size());
    printf("entity %s %s\n", entity.c_str(), value ? value : "<null>");
  }
}
