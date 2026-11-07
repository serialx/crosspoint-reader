#pragma once

#include <xp_http_transfer.h>

#include <string>

namespace httptransfer {
XpHttpNativeText nativeText(const std::string& value);
XpHttpMutableText mutableText(std::string& value);
const XpHttpLeaves& leaves();
}  // namespace httptransfer
