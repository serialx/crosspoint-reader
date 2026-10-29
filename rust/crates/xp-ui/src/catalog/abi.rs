use super::{
    ffi::{DataRow, Services},
    hits::Hits,
    state::State,
    view::Layout,
};
use core::mem::{offset_of, size_of};
const _: () = {
    assert!(size_of::<State>() == 68);
    assert!(offset_of!(State, mode) == 61);
    assert!(size_of::<Hits>() == 288);
    assert!(offset_of!(Hits, controls) == 140);
    assert!(size_of::<Layout>() == 84);
    assert!(offset_of!(Layout, search_button) == 48);
    assert!(size_of::<DataRow>() == 4 * size_of::<usize>());
    assert!(offset_of!(Services, count) == 6 * size_of::<usize>());
};
