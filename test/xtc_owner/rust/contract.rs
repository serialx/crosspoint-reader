#[test]
fn c3_profile_text_and_owner_layout_are_frozen() {
    use crate::xtc_owner::{abi::*, Owner};
    use core::mem::{align_of, size_of};
    assert_eq!(
        size_of::<XpXtcChapterRow>(),
        if size_of::<usize>() == 4 { 28 } else { 40 }
    );
    assert_eq!(size_of::<Owner>(), size_of::<XpXtcOwnerStorage>());
    assert_eq!(align_of::<Owner>(), align_of::<XpXtcOwnerStorage>());
}
