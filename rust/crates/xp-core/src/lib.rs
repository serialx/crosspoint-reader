#![no_std]

pub mod bidi;
pub mod chapter;
pub mod css;
pub mod epub;
pub mod font;
pub mod gif;
pub mod glyph;
pub mod hyphen;
pub mod i18n;
pub mod image;
pub mod inflate;
pub mod library;
pub mod page;
pub mod para;
pub mod png;
pub mod sdfont;
pub mod section;
pub mod text;
pub mod textm;
pub mod utf8;
pub mod xml;
pub mod xtc;
pub mod zip;

/// FNV-1a 64, matching ZipFile::fnvHash64 over raw bytes.
pub fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 14_695_981_039_346_656_037u64;
    for &byte in bytes {
        hash ^= u64::from(byte);
        hash = hash.wrapping_mul(1_099_511_628_211);
    }
    hash
}

/// CRC-32/ISO-HDLC (IEEE), matching miniz mz_crc32(0, bytes, len).
pub fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for &byte in bytes {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fnv_known_vectors() {
        assert_eq!(fnv1a64(b""), 0xcbf2_9ce4_8422_2325);
        assert_eq!(fnv1a64(b"a"), 0xaf63_dc4c_8601_ec8c);
        assert_eq!(fnv1a64(b"foobar"), 0x8594_4171_f739_67e8);
        assert_eq!(fnv1a64(&[0]), 0xaf63_bd4c_8601_b7df);
        assert_eq!(fnv1a64(&[0, 0x7f, 0x80, 0xff, 0, 1]), 0x31f7_d55d_e14a_fa42);
        assert_eq!(fnv1a64(&[0xff, 0x80, 0, 0x7f]), 0xbe1e_db67_05cc_2dd3);
    }

    #[test]
    fn crc_known_vectors() {
        assert_eq!(crc32(b""), 0);
        assert_eq!(crc32(b"123456789"), 0xcbf4_3926);
        assert_eq!(
            crc32(b"The quick brown fox jumps over the lazy dog"),
            0x414f_a339
        );
        assert_eq!(crc32(&[0]), 0xd202_ef8d);
        assert_eq!(crc32(&[0xff]), 0xff00_0000);
        assert_eq!(crc32(&[0, 0x7f, 0x80, 0xff, 0, 1]), 0xee86_64fd);
        assert_eq!(crc32(&[0xff, 0x80, 0, 0x7f]), 0xdece_aec0);
    }
}

pub mod strip;

pub mod sync;

pub mod ota;
