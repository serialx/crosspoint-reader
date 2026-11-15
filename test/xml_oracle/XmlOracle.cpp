#include <expat.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

struct Event {
  std::string kind;
  XML_Index byte;
  XML_Size line;
  std::vector<std::string> data;
};

struct Recorder {
  XML_Parser parser;
  std::vector<Event> events;
  size_t stopAfter;

  void append(const char* kind, std::vector<std::string> data = {}) {
    if (std::string(kind) == "text" && !events.empty() && events.back().kind == "text") {
      events.back().data.front() += data.front();
      return;
    }
    events.push_back({kind, XML_GetCurrentByteIndex(parser), XML_GetCurrentLineNumber(parser), std::move(data)});
    if (stopAfter && events.size() == stopAfter) XML_StopParser(parser, XML_FALSE);
  }
};

static void XMLCALL start(void* ctx, const char* name, const char** attrs) {
  std::vector<std::string> data{name};
  for (size_t i = 0; attrs[i]; ++i) data.emplace_back(attrs[i]);
  static_cast<Recorder*>(ctx)->append("start", std::move(data));
}
static void XMLCALL end(void* ctx, const char* name) { static_cast<Recorder*>(ctx)->append("end", {name}); }
static void XMLCALL text(void* ctx, const char* value, int length) {
  static_cast<Recorder*>(ctx)->append("text", {std::string(value, length)});
}
static void XMLCALL comment(void* ctx, const char* value) { static_cast<Recorder*>(ctx)->append("comment", {value}); }
static void XMLCALL pi(void* ctx, const char* target, const char* value) {
  static_cast<Recorder*>(ctx)->append("pi", {target, value});
}
static void XMLCALL cdataStart(void* ctx) { static_cast<Recorder*>(ctx)->append("cdata_start"); }
static void XMLCALL cdataEnd(void* ctx) { static_cast<Recorder*>(ctx)->append("cdata_end"); }
static void XMLCALL otherwise(void* ctx, const char* value, int length) {
  if (length >= 3 && value[0] == '&' && value[length - 1] == ';') {
    static_cast<Recorder*>(ctx)->append("entity", {std::string(value + 1, length - 2)});
  }
}

static void quoted(const std::string& value) {
  const char hex[] = "0123456789abcdef";
  std::cout << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\')
      std::cout << '\\' << c;
    else if (c < 32)
      std::cout << "\\u00" << hex[c >> 4] << hex[c & 15];
    else
      std::cout << c;
  }
  std::cout << '"';
}

// One input can be replayed through many split points without process startup overhead.
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 2;
  const std::string input((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::string mode = argv[2];
  const size_t amount = std::strtoull(argv[3], nullptr, 10);
  const size_t stopAfter = std::strtoull(argv[4], nullptr, 10);
  const size_t iterations = mode == "splits" ? input.size() + 1 : 1;
  for (size_t split = 0; split < iterations; ++split) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return 3;
    Recorder recorder{parser, {}, stopAfter};
    XML_SetUserData(parser, &recorder);
    XML_SetElementHandler(parser, start, end);
    XML_SetCharacterDataHandler(parser, text);
    XML_SetCommentHandler(parser, comment);
    XML_SetProcessingInstructionHandler(parser, pi);
    XML_SetCdataSectionHandler(parser, cdataStart, cdataEnd);
    XML_SetDefaultHandlerExpand(parser, otherwise);
    XML_Status status = XML_STATUS_OK;
    size_t pos = 0;
    if (mode == "splits") {
      status = XML_Parse(parser, input.data(), static_cast<int>(split), XML_FALSE);
      pos = split;
    }
    while (status == XML_STATUS_OK) {
      const size_t count = mode == "chunks" ? std::min(amount, input.size() - pos) : input.size() - pos;
      const bool final = pos + count == input.size();
      status = XML_Parse(parser, input.data() + pos, static_cast<int>(count), final);
      pos += count;
      if (final) break;
    }
    std::cout << "{\"events\":[";
    bool first = true;
    for (const auto& event : recorder.events) {
      if (!first) std::cout << ',';
      first = false;
      std::cout << '[';
      quoted(event.kind);
      std::cout << ',' << event.byte << ',' << event.line;
      for (const auto& value : event.data) {
        std::cout << ',';
        quoted(value);
      }
      std::cout << ']';
    }
    XML_ParsingStatus parsing{};
    XML_GetParsingStatus(parser, &parsing);
    std::cout << "],\"status\":" << status << ",\"error\":" << XML_GetErrorCode(parser)
              << ",\"byte\":" << XML_GetCurrentByteIndex(parser) << ",\"line\":" << XML_GetCurrentLineNumber(parser)
              << ",\"state\":" << parsing.parsing << ",\"final\":" << (parsing.finalBuffer ? "true" : "false") << "}\n";
    XML_ParserFree(parser);
  }
}
