#include "HttpHeader.h"

#include "HttpText.h"

void parseHttpHeaderLine(const std::string_view line, HttpHeader& header) {
  auto name = httptransfer::mutableText(header.name);
  auto value = httptransfer::mutableText(header.value);
  xp_http_header_parse_retire_c1_s2({reinterpret_cast<const uint8_t*>(line.data()), line.size()}, &name, &value,
                                    &httptransfer::textLeaves);
}

std::string formatHttpHeaderLine(const HttpHeader& header, const bool maskValue) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_http_header_format_retire_c1_s2(httptransfer::nativeText(header.name), httptransfer::nativeText(header.value),
                                       maskValue, output, &httptransfer::textLeaves);
  });
}
