// Hypher tries: https://github.com/typst/hypher (MIT/Apache-2.0).
// Pattern licenses are listed in docs/hyphenation-trie-format.md.
#[repr(align(4))]
struct Aligned<const N: usize>([u8; N]);
static DE: Aligned<206263> = Aligned(*include_bytes!("../../data/hyph/de.bin"));
static EN: Aligned<26947> = Aligned(*include_bytes!("../../data/hyph/en.bin"));
static ES: Aligned<13649> = Aligned(*include_bytes!("../../data/hyph/es.bin"));
static FI: Aligned<1254> = Aligned(*include_bytes!("../../data/hyph/fi.bin"));
static FR: Aligned<6988> = Aligned(*include_bytes!("../../data/hyph/fr.bin"));
static IT: Aligned<1555> = Aligned(*include_bytes!("../../data/hyph/it.bin"));
static PL: Aligned<15530> = Aligned(*include_bytes!("../../data/hyph/pl.bin"));
static PT: Aligned<1065> = Aligned(*include_bytes!("../../data/hyph/pt.bin"));
static RU: Aligned<33344> = Aligned(*include_bytes!("../../data/hyph/ru.bin"));
static SV: Aligned<23595> = Aligned(*include_bytes!("../../data/hyph/sv.bin"));
static UK: Aligned<21312> = Aligned(*include_bytes!("../../data/hyph/uk.bin"));
pub(super) fn trie(language: i32) -> Option<&'static [u8]> {
    match language {
        0 => Some(&EN.0),
        1 => Some(&FR.0),
        2 => Some(&DE.0),
        3 => Some(&RU.0),
        4 => Some(&ES.0),
        5 => Some(&IT.0),
        6 => Some(&PL.0),
        7 => Some(&SV.0),
        8 => Some(&UK.0),
        9 => Some(&FI.0),
        10 => Some(&PT.0),
        _ => None,
    }
}
