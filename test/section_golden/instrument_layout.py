#!/usr/bin/env python3
"""Attach the host-only Rust trace sink; require its adapter anchor exactly once."""
import sys
from pathlib import Path


def instrument(source):
    anchor = "  xp_para_layout(handle(), &metrics, &options);"
    if source.count(anchor) != 1:
        raise ValueError(f"Layout trace anchor must occur once: {anchor!r}")
    sink = """  metrics.trace = [](void*, XpParaSpan record) {
    LayoutTrace::stream.write(reinterpret_cast<const char*>(record.data), record.len);
  };
"""
    return '#include "LayoutTrace.h"\n' + source.replace(anchor, sink + anchor)


if __name__ == '__main__':
    Path(sys.argv[2]).write_text(instrument(Path(sys.argv[1]).read_text()))
