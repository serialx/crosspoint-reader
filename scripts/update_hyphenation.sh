#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

process() {
  local lang="$1"

  mkdir -p "build"
  wget -O "build/$lang.bin" "https://github.com/typst/hypher/raw/refs/heads/main/tries/$lang.bin"

  python3 scripts/generate_hyphenation_trie.py \
    --input "build/$lang.bin" \
    --output "rust/crates/xp-core/data/hyph/${lang}.bin"
}

process en
process fr
process de
process es
process ru
process it
process uk
process pl
process pt
process sv
process fi
