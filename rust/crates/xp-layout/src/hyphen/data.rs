// Hypher: https://github.com/typst/hypher; code is MIT/Apache-2.0.
// Tries retain the individual pattern licenses; see docs/hyphenation-trie-format.md.
#[repr(align(4))]
struct Aligned<const N: usize>([u8; N]);
static DE: Aligned<{ include_bytes!("../../data/hyph/de.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/de.bin"));
static EN: Aligned<{ include_bytes!("../../data/hyph/en.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/en.bin"));
static ES: Aligned<{ include_bytes!("../../data/hyph/es.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/es.bin"));
static FI: Aligned<{ include_bytes!("../../data/hyph/fi.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/fi.bin"));
static FR: Aligned<{ include_bytes!("../../data/hyph/fr.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/fr.bin"));
static IT: Aligned<{ include_bytes!("../../data/hyph/it.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/it.bin"));
static PL: Aligned<{ include_bytes!("../../data/hyph/pl.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/pl.bin"));
static PT: Aligned<{ include_bytes!("../../data/hyph/pt.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/pt.bin"));
static RU: Aligned<{ include_bytes!("../../data/hyph/ru.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/ru.bin"));
static SV: Aligned<{ include_bytes!("../../data/hyph/sv.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/sv.bin"));
static UK: Aligned<{ include_bytes!("../../data/hyph/uk.bin").len() }> =
    Aligned(*include_bytes!("../../data/hyph/uk.bin"));
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
