#pragma once
#include <Print.h>
#include <XmlPullParser.h>

#include <string>

class ContainerParser final : public Print {
  enum ParserState {
    START,
    IN_CONTAINER,
    IN_ROOTFILES,
  };

  size_t remainingSize;
  XmlPullParser parser;
  ParserState state = START;

  static void startElement(void* userData, const char* name, const char* const* atts);
  static void endElement(void* userData, const char* name);

 public:
  std::string fullPath;

  explicit ContainerParser(const size_t xmlSize) : remainingSize(xmlSize) {}
  ~ContainerParser() override = default;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
