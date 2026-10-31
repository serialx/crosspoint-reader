#pragma once

#include <Logging.h>
#include <xp_ffi.h>

#include <climits>
#include <cstddef>
#include <cstdint>

// Drives borrowed Rust events synchronously; handlers must copy retained values.
class XmlPullParser {
 public:
  using StartHandler = void (*)(void*, const char*, const char* const*);
  using EndHandler = void (*)(void*, const char*);
  using TextHandler = void (*)(void*, const char*, int);

  XmlPullParser() = default;
  XmlPullParser(const XmlPullParser&) = delete;
  XmlPullParser& operator=(const XmlPullParser&) = delete;
  ~XmlPullParser() { reset(); }

  bool setup(void* context, StartHandler start, EndHandler end = nullptr, TextHandler text = nullptr) {
    reset();
    this->context = context;
    this->start = start;
    this->end = end;
    this->text = text;
    if (xp_xml_create(&parser) != 0) {
      LOG_ERR("XML", "Could not allocate parser");
      return false;
    }
    return true;
  }

  explicit operator bool() const { return parser != nullptr; }
  void reset() {
    xp_xml_destroy(parser);
    parser = nullptr;
    completed = false;
  }

  bool write(const uint8_t* data, size_t size, bool final) {
    if (!parser || completed) return false;
    size_t offset = 0;
    do {
      size_t consumed = 0;
      if (xp_xml_feed(parser, data ? data + offset : nullptr, size - offset, final, &consumed) != 0) return false;
      offset += consumed;
      for (;;) {
        XpXmlEvent event{};
        if (xp_xml_next(parser, &event) != 0) {
          LOG_ERR("XML", "Could not read parser event");
          return false;
        }
        switch (event.kind) {
          case XP_XML_START:
            if (start) start(context, event.name.data, event.attributes);
            break;
          case XP_XML_END:
            if (end) end(context, event.name.data);
            break;
          case XP_XML_TEXT:
            if (event.name.len > INT_MAX) return false;
            if (text) text(context, event.name.data, static_cast<int>(event.name.len));
            break;
          case XP_XML_COMMENT:
          case XP_XML_PI:
          case XP_XML_CDATA_START:
          case XP_XML_CDATA_END:
          case XP_XML_ENTITY:
            break;
          case XP_XML_DONE:
            completed = true;
            return offset == size;
          case XP_XML_ERROR:
            LOG_ERR("XML", "Parse error %u at byte %llu, line %llu", event.error,
                    static_cast<unsigned long long>(event.byte), static_cast<unsigned long long>(event.line));
            return false;
          case XP_XML_STOPPED:
            return false;
          case XP_XML_NEED_INPUT:
            break;
          default:
            return false;
        }
        if (event.kind == XP_XML_NEED_INPUT) break;
      }
      if (consumed == 0 && offset < size) return false;
    } while (offset < size);
    return true;
  }

 private:
  XpXml* parser = nullptr;
  bool completed = false;
  void* context = nullptr;
  StartHandler start = nullptr;
  EndHandler end = nullptr;
  TextHandler text = nullptr;
};
