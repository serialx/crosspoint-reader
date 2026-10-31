//! Huffman storage policies for the shared inflate state machine.
use super::*;

#[doc(hidden)]
mod sealed {
    pub trait Sealed {}
    impl Sealed for super::FastTables {}
    impl Sealed for super::SmallTables {}
}

pub trait HuffmanStorage: sealed::Sealed + Clone + Default {
    fn lengths(&mut self, table: usize) -> &mut [u8];
    fn codes(&mut self) -> &mut [u8];
    fn finish_lengths(&mut self, literal: usize, distance: usize);
    fn build(&mut self, table: usize, size: usize) -> bool;
    fn lookup(&self, table: usize, bits: BitBuffer) -> (i32, u32);
    fn available(&self, table: usize, bits: BitBuffer, count: u32) -> bool;
}

#[derive(Clone)]
pub struct FastTables {
    /// Huffman tables.
    tables: [HuffmanTable; MAX_HUFF_TABLES],
    trees: [i16; MAX_HUFF_TREE_SIZE + MAX_HUFF_SYMBOLS_1 * 2],

    #[cfg_attr(feature = "serde", serde(with = "BigArray"))]
    code_size_literal: [u8; MAX_HUFF_SYMBOLS_0],
    code_size_dist: [u8; MAX_HUFF_SYMBOLS_1],
    code_size_huffman: [u8; MAX_HUFF_SYMBOLS_2],
    /// Huffman length codes.
    #[cfg_attr(feature = "serde", serde(with = "BigArray"))]
    // MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1 + 137
    // Extended to 512 to allow masking to help evade bounds checks.
    len_codes: [u8; LEN_CODES_SIZE],
}
impl Default for FastTables {
    fn default() -> Self {
        Self {
            trees: [0; MAX_HUFF_TREE_SIZE + MAX_HUFF_SYMBOLS_1 * 2],
            tables: [
                HuffmanTable::new(),
                HuffmanTable::new(),
                HuffmanTable::new(),
            ],
            code_size_literal: [0; MAX_HUFF_SYMBOLS_0],
            code_size_dist: [0; MAX_HUFF_SYMBOLS_1],
            code_size_huffman: [0; MAX_HUFF_SYMBOLS_2],
            len_codes: [0; LEN_CODES_SIZE],
        }
    }
}
impl FastTables {
    #[inline]
    fn tree(&self, table: usize) -> &[i16] {
        if table == LITLEN_TABLE {
            &self.trees[..MAX_HUFF_TREE_SIZE]
        } else {
            &self.trees[MAX_HUFF_TREE_SIZE..]
        }
    }
}
impl HuffmanStorage for FastTables {
    #[inline]
    fn lengths(&mut self, table: usize) -> &mut [u8] {
        match table {
            LITLEN_TABLE => &mut self.code_size_literal,
            DIST_TABLE => &mut self.code_size_dist,
            _ => &mut self.code_size_huffman,
        }
    }
    #[inline]
    fn codes(&mut self) -> &mut [u8] {
        &mut self.len_codes
    }
    #[inline]
    fn finish_lengths(&mut self, literal: usize, distance: usize) {
        self.code_size_literal[..literal].copy_from_slice(&self.len_codes[..literal]);
        self.code_size_dist[..distance]
            .copy_from_slice(&self.len_codes[literal..literal + distance]);
    }
    fn build(&mut self, bt: usize, table_size: usize) -> bool {
        let code_sizes = match bt {
            LITLEN_TABLE => &mut self.code_size_literal[..],
            DIST_TABLE => &mut self.code_size_dist,
            HUFFLEN_TABLE => &mut self.code_size_huffman,
            _ => return false,
        };
        let table = &mut self.tables[bt];
        let (literal_tree, distance_tree) = self.trees.split_at_mut(MAX_HUFF_TREE_SIZE);
        let tree = if bt == LITLEN_TABLE {
            literal_tree
        } else {
            distance_tree
        };

        let mut total_symbols = [0u16; 16];
        // Next code - we use the odd length here to simplify a loop later.
        let mut next_code = [0u32; 17];
        const INVALID_CODE: i16 = (1 << 9) | 286;
        // Set the values in the fast table to return a
        // non-zero length and an invalid symbol instead of zero
        // so that we do not have to have a check for a zero
        // code length in the hot code path later
        // and can instead error out on the invalid symbol check
        // on bogus input.
        table.look_up.fill(INVALID_CODE);
        // If we are initializing the huffman code length we can skip
        // this since these codes can't be longer than 3 bits
        // and thus only use the fast table and this table won't be accessed so
        // there is no point clearing it.
        // TODO: Avoid creating this table at all.
        if bt != HUFFLEN_TABLE {
            tree.fill(0);
        }

        if table_size > code_sizes.len() {
            return false;
        }

        for &code_size in &code_sizes[..table_size] {
            let cs = code_size as usize;
            // Code sizes are limited to max 15 according to the
            // deflate spec.
            // If it is larger than this, something has gone wrong...
            if cs >= total_symbols.len() {
                return false;
            }
            total_symbols[cs] += 1;
        }

        let mut total = 0u32;
        let mut max_code_len = 0u32;
        // Count up the total number of used lengths and check that the table
        // is not over-subscribed at any step (matching zlib's inftrees.c).
        {
            let mut left = 1i32;
            for (i, (&ts, next)) in total_symbols
                .iter()
                .zip(next_code[1..].iter_mut())
                .enumerate()
                .skip(1)
            {
                if ts > 0 {
                    max_code_len = i as u32;
                }
                total += u32::from(ts);
                total <<= 1;
                *next = total;

                left <<= 1;
                left -= i32::from(ts);
                if left < 0 {
                    // Over-subscribed: too many codes for the available bit space.
                    return false;
                }
            }
        }

        // A complete Huffman code has total == 65536 (2^16 after left-shifting
        // through all 15 possible code lengths). If total != 65536 the code is
        // incomplete (unused bit patterns remain).
        //
        // Per RFC 1951 and zlib's inftrees.c:
        // - Code length (hufflen) tables must always be complete.
        // - Literal/length and distance tables may be incomplete when
        //   max_code_len <= 1: either all symbols are unused (max_code_len == 0,
        //   e.g. a distance table with no backreferences) or a single symbol is
        //   encoded with a 1-bit code (e.g. an EOB-only litlen table).
        //   Tables with max_code_len > 1 that are incomplete are rejected, as
        //   they have multi-bit codes that don't form a valid prefix code.
        if total != 65_536 && (bt == HUFFLEN_TABLE || max_code_len > 1) {
            return false;
        }

        let mut tree_next = -1;
        for (symbol_index, &size) in code_sizes.iter().take(table_size).enumerate() {
            // Code sizes are limited to 15 according to the spec
            // It's already checked earlier but the compiler might not be smart enough to know that.
            let code_size = size & 15;
            if code_size == 0 {
                continue;
            }

            let cur_code = next_code[code_size as usize];
            next_code[code_size as usize] += 1;

            let n = (cur_code & (u32::MAX >> (32 - code_size))) as u16;

            let mut rev_code = if n < 512 {
                // Using a lookup table
                // for a small speedup here,
                // Seems to only really make a difference on very short
                // inputs however.
                // 512 seems to be around a sweet spot.
                reverse_bits(n)
            } else {
                n.reverse_bits()
            } >> (16 - code_size);

            if code_size <= FAST_LOOKUP_BITS {
                let k = (i16::from(code_size) << 9) | symbol_index as i16;
                while rev_code < FAST_LOOKUP_SIZE {
                    table.look_up[rev_code as usize] = k;
                    rev_code += 1 << code_size;
                }
                continue;
            }

            let mut tree_cur = table.look_up[(rev_code & (FAST_LOOKUP_SIZE - 1)) as usize];
            if tree_cur == INVALID_CODE {
                table.look_up[(rev_code & (FAST_LOOKUP_SIZE - 1)) as usize] = tree_next;
                tree_cur = tree_next;
                tree_next -= 2;
            }

            rev_code >>= FAST_LOOKUP_BITS - 1;
            for _ in FAST_LOOKUP_BITS + 1..code_size {
                rev_code >>= 1;
                tree_cur -= (rev_code & 1) as i16;
                let tree_index = (-tree_cur - 1) as usize;
                if tree_index >= tree.len() {
                    return false;
                }
                if tree[tree_index] == 0 {
                    tree[tree_index] = tree_next;
                    tree_cur = tree_next;
                    tree_next -= 2;
                } else {
                    tree_cur = tree[tree_index];
                }
            }

            rev_code >>= 1;
            tree_cur -= (rev_code & 1) as i16;
            let tree_index = (-tree_cur - 1) as usize;
            if tree_index >= tree.len() {
                return false;
            }
            tree[tree_index] = symbol_index as i16;
        }

        true
    }
    #[inline]
    fn lookup(&self, table: usize, bits: BitBuffer) -> (i32, u32) {
        self.tables[table].lookup(self.tree(table), bits)
    }
    #[inline]
    fn available(&self, table: usize, bits: BitBuffer, count: u32) -> bool {
        let mut symbol = i32::from(self.tables[table].fast_lookup(bits));
        if symbol >= 0 {
            let length = (symbol >> 9) as u32;
            return length != 0 && count >= length;
        }
        if count <= FAST_LOOKUP_BITS.into() {
            return false;
        }
        let mut length = u32::from(FAST_LOOKUP_BITS);
        loop {
            symbol =
                i32::from(self.tree(table)[(!symbol + ((bits >> length) & 1) as i32) as usize]);
            length += 1;
            if symbol >= 0 || count < length + 1 {
                return symbol >= 0;
            }
        }
    }
}

/// Compact canonical tables. The code-length alphabet is dead before the
/// distance table is built, so both use slot 1. Code-length lengths are dead
/// before the literal/distance lengths are read, so those share storage too.
#[derive(Clone)]
pub struct SmallTables {
    counts: [[u16; 16]; 2],
    symbols: [u16; 320],
    lengths: [u8; 320],
}
impl Default for SmallTables {
    fn default() -> Self {
        Self {
            counts: [[0; 16]; 2],
            symbols: [0; 320],
            lengths: [0; 320],
        }
    }
}
impl HuffmanStorage for SmallTables {
    #[inline]
    fn lengths(&mut self, table: usize) -> &mut [u8] {
        match table {
            LITLEN_TABLE => &mut self.lengths[..288],
            DIST_TABLE => &mut self.lengths[288..],
            _ => &mut self.lengths[..19],
        }
    }
    #[inline]
    fn codes(&mut self) -> &mut [u8] {
        &mut self.lengths
    }
    fn finish_lengths(&mut self, literal: usize, distance: usize) {
        self.lengths.copy_within(literal..literal + distance, 288);
    }
    fn build(&mut self, table: usize, size: usize) -> bool {
        let mut counts = [0u16; 16];
        let lengths = self.lengths(table);
        if size > lengths.len() {
            return false;
        }
        for &length in &lengths[..size] {
            if length > 15 {
                return false;
            }
            counts[length as usize] += 1;
        }
        let mut left = 1i32;
        let mut max = 0;
        let mut offsets = [0u16; 16];
        for i in 1..16 {
            left = (left << 1) - i32::from(counts[i]);
            if left < 0 {
                return false;
            }
            if counts[i] != 0 {
                max = i;
            }
            if i < 15 {
                offsets[i + 1] = offsets[i] + counts[i];
            }
        }
        if left != 0 && (table == HUFFLEN_TABLE || max > 1) {
            return false;
        }
        self.counts[usize::from(table != LITLEN_TABLE)] = counts;
        let base = if table == LITLEN_TABLE { 0 } else { 288 };
        for symbol in 0..size {
            let length = self.lengths(table)[symbol] as usize;
            if length != 0 {
                self.symbols[base + offsets[length] as usize] = symbol as u16;
                offsets[length] += 1;
            }
        }
        true
    }
    #[inline]
    fn lookup(&self, table: usize, mut bits: BitBuffer) -> (i32, u32) {
        let counts = &self.counts[usize::from(table != LITLEN_TABLE)];
        let mut code = 0;
        let mut first = 0;
        let mut index = if table == LITLEN_TABLE { 0 } else { 288 };
        for (length, &count) in counts.iter().enumerate().skip(1) {
            code = (code << 1) | (bits as u32 & 1);
            bits >>= 1;
            let count = u32::from(count);
            if code < first + count {
                return (
                    i32::from(self.symbols[index + (code - first) as usize]),
                    length as u32,
                );
            }
            index += count as usize;
            first = (first + count) << 1;
        }
        (286, 15)
    }
    #[inline]
    fn available(&self, table: usize, bits: BitBuffer, count: u32) -> bool {
        self.lookup(table, bits).1 <= count
    }
}
