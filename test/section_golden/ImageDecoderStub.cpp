#include <Epub/converters/ImageDecoderFactory.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  const auto dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif";
}

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) {
  std::fputs("Section oracle unexpectedly requested pixel decoding\n", stderr);
  std::exit(EXIT_FAILURE);
}
