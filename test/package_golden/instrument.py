#!/usr/bin/env python3
"""Add host-only observations without replacing cache serialization."""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
prefix = '''#include <fstream>
#include <string_view>
extern std::ofstream packageCalls;
void packageField(std::string_view);
'''
pattern = r'((?:bool|void) BookMetadataCache::(\w+)\([^)]*\)(?: const)? \{)'


def observe(match):
    name = match[2]
    if name in ('createSpineEntry', 'createTocEntry', 'observe'):
        return match[1]
    return match[1] + f'\n  packageCalls << "{name} " << \'\\n\';'

source = source.replace('void BookMetadataCache::observe(void*, uint32_t, XpPkgSpan, XpPkgSpan, XpPkgSpan, uint8_t) {}', r'''void BookMetadataCache::observe(void*, uint32_t kind, XpPkgSpan title, XpPkgSpan href, XpPkgSpan anchor, uint8_t level) {
  const auto field = [](XpPkgSpan v) { packageField(std::string_view(reinterpret_cast<const char*>(v.data), v.len)); };
  if (kind == 0) { packageCalls << "createSpineEntry "; field(href); }
  else { packageCalls << "createTocEntry "; field(title); field(href); field(anchor); packageCalls << unsigned(level); }
  packageCalls << '\n';
}''')
source = source.replace('seek, nullptr, nullptr};', 'seek, nullptr, observe};')
Path(sys.argv[2]).write_text(prefix + re.sub(pattern, observe, source))
