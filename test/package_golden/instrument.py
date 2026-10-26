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
    extra = ''
    if name == 'createSpineEntry':
        extra = ' packageField(href);'
    elif name == 'createTocEntry':
        extra = ' packageField(title); packageField(href); packageField(anchor); packageCalls << unsigned(level);'
    return match[1] + '\n  packageCalls << "' + name + ' ";' + extra + " packageCalls << '\\n';"


Path(sys.argv[2]).write_text(prefix + re.sub(pattern, observe, source))
