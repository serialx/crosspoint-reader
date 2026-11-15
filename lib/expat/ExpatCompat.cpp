#include <Logging.h>
#include <Memory.h>
#include <xp_ffi.h>

#include <climits>
#include <cstdint>

#include "expat.h"

#if defined(XML_UNICODE) || defined(XML_DTD) || (defined(XML_GE) && XML_GE != 0)
#error "The SDK XML adapter supports narrow characters and GE=0 only"
#endif

struct XML_ParserStruct {
  // XML_GetUserData's public macro requires this to be the first member.
  void* userData = nullptr;
  XpXml* core = nullptr;
  XML_StartElementHandler start = nullptr;
  XML_EndElementHandler end = nullptr;
  XML_CharacterDataHandler text = nullptr;
  std::unique_ptr<char[]> buffer;
  size_t capacity = 0;
  XML_Size line = 1;
  XML_Error error = XML_ERROR_NONE;
  bool started = false;
  bool finished = false;
  bool stopped = false;

  ~XML_ParserStruct() { xp_xml_destroy(core); }
};

namespace {
XML_Status fail(XML_Parser parser, XML_Error error) {
  parser->error = error;
  return XML_STATUS_ERROR;
}
}  // namespace

XML_Parser XMLCALL XML_ParserCreate(const XML_Char* encoding) {
  // Active SDK callers use encoding detection from the document.
  if (encoding) return nullptr;
  auto parser = makeUniqueNoThrow<XML_ParserStruct>();
  if (!parser || xp_xml_create(&parser->core) != 0) {
    LOG_ERR("XML", "Could not allocate SDK parser (%u bytes)", static_cast<unsigned>(sizeof(XML_ParserStruct)));
    return nullptr;
  }
  // The SDK owns this C handle and releases it with XML_ParserFree.
  return parser.release();
}

void XMLCALL XML_ParserFree(XML_Parser parser) { delete parser; }

void XMLCALL XML_SetUserData(XML_Parser parser, void* userData) { parser->userData = userData; }

void XMLCALL XML_SetElementHandler(XML_Parser parser, XML_StartElementHandler start, XML_EndElementHandler end) {
  parser->start = start;
  parser->end = end;
}

void XMLCALL XML_SetCharacterDataHandler(XML_Parser parser, XML_CharacterDataHandler text) { parser->text = text; }

XML_Status XMLCALL XML_Parse(XML_Parser parser, const char* data, int length, int final) {
  if (!parser) return XML_STATUS_ERROR;
  if (length < 0 || (!data && length)) return fail(parser, XML_ERROR_INVALID_ARGUMENT);
  if (parser->finished) return fail(parser, XML_ERROR_FINISHED);
  if (parser->stopped) return fail(parser, XML_ERROR_ABORTED);
  if (parser->error != XML_ERROR_NONE) return XML_STATUS_ERROR;
  parser->started = true;
  size_t offset = 0;
  do {
    size_t consumed = 0;
    if (xp_xml_feed(parser->core, data ? reinterpret_cast<const uint8_t*>(data + offset) : nullptr,
                    static_cast<size_t>(length) - offset, final != 0, &consumed) != 0) {
      return fail(parser, XML_ERROR_INVALID_ARGUMENT);
    }
    offset += consumed;
    for (;;) {
      XpXmlEvent event{};
      if (xp_xml_next(parser->core, &event) != 0) return fail(parser, XML_ERROR_NO_MEMORY);
      if (event.kind == XP_XML_NEED_INPUT) break;
      parser->line = static_cast<XML_Size>(event.line);
      switch (event.kind) {
        case XP_XML_START:
          if (parser->start)
            parser->start(parser->userData, event.name.data, const_cast<const char**>(event.attributes));
          break;
        case XP_XML_END:
          if (parser->end) parser->end(parser->userData, event.name.data);
          break;
        case XP_XML_TEXT:
          if (event.name.len > INT_MAX) return fail(parser, XML_ERROR_NO_MEMORY);
          if (parser->text) parser->text(parser->userData, event.name.data, static_cast<int>(event.name.len));
          break;
        case XP_XML_COMMENT:
        case XP_XML_PI:
        case XP_XML_CDATA_START:
        case XP_XML_CDATA_END:
        case XP_XML_ENTITY:
          break;
        case XP_XML_ERROR:
          return fail(parser, static_cast<XML_Error>(event.error));
        case XP_XML_DONE:
          parser->finished = true;
          return XML_STATUS_OK;
        case XP_XML_STOPPED:
          return fail(parser, XML_ERROR_ABORTED);
        default:
          return fail(parser, XML_ERROR_UNEXPECTED_STATE);
      }
      if (parser->stopped) {
        // Wait until the callback releases borrowed event spans before stopping.
        xp_xml_stop(parser->core);
        return fail(parser, XML_ERROR_ABORTED);
      }
    }
    if (!consumed && offset < static_cast<size_t>(length)) return fail(parser, XML_ERROR_NO_MEMORY);
  } while (offset < static_cast<size_t>(length));
  return XML_STATUS_OK;
}

void* XMLCALL XML_GetBuffer(XML_Parser parser, int length) {
  if (!parser) return nullptr;
  if (length < 0 || parser->finished || parser->stopped || parser->error != XML_ERROR_NONE) {
    if (length < 0) fail(parser, XML_ERROR_INVALID_ARGUMENT);
    return nullptr;
  }
  const size_t capacity = length ? static_cast<size_t>(length) : 1;
  if (capacity > parser->capacity) {
    auto buffer = makeUniqueNoThrow<char[]>(capacity);
    if (!buffer) {
      LOG_ERR("XML", "Could not allocate SDK input buffer (%u bytes)", static_cast<unsigned>(capacity));
      fail(parser, XML_ERROR_NO_MEMORY);
      return nullptr;
    }
    parser->buffer = std::move(buffer);
    parser->capacity = capacity;
  }
  return parser->buffer.get();
}

XML_Status XMLCALL XML_ParseBuffer(XML_Parser parser, int length, int final) {
  if (!parser) return XML_STATUS_ERROR;
  if (!parser->buffer) return fail(parser, XML_ERROR_NO_BUFFER);
  if (length < 0 || static_cast<size_t>(length) > parser->capacity) return fail(parser, XML_ERROR_INVALID_ARGUMENT);
  return XML_Parse(parser, parser->buffer.get(), length, final);
}

XML_Status XMLCALL XML_StopParser(XML_Parser parser, XML_Bool resumable) {
  if (!parser) return XML_STATUS_ERROR;
  if (!parser->started) return fail(parser, XML_ERROR_NOT_STARTED);
  if (parser->finished) return fail(parser, XML_ERROR_FINISHED);
  if (resumable) return fail(parser, XML_ERROR_INVALID_ARGUMENT);
  parser->stopped = true;
  return XML_STATUS_OK;
}

XML_Error XMLCALL XML_GetErrorCode(XML_Parser parser) { return parser->error; }
XML_Size XMLCALL XML_GetCurrentLineNumber(XML_Parser parser) { return parser->line; }
