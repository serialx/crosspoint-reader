//! Conservative idle admission while the renderer owns the framebuffer.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Snapshot {
    pub spine: i32,
    pub spine_count: i32,
    pub confirm_threshold: u32,
    pub busy: u8,
    pub has_epub: u8,
    pub load_popup: u8,
    pub remembered: u8,
    pub end_options: u8,
    pub touch: u8,
    pub remote: u8,
    pub home_action: u8,
    pub edges: u8,
    pub held_buttons: u8,
    pub overlay: u8,
    pub automatic: u8,
    pub pending_turn: i8,
    pub bookmark_notice: u8,
    pub dictionary_notice: u8,
    pub font_notice: u8,
    pub clipping_notice: u8,
    pub read_move: u8,
    pub recents_removed: u8,
    pub applied_orientation: u8,
    pub orientation: u8,
    pub long_button: u8,
    pub short_power: u8,
    pub tilt: u8,
    pub footnote_depth: u8,
    pub touch_controls: u8,
    pub reserved: [u8; 2],
}
pub const ADMITTED: u8 = 1;
pub const SCREEN_LONG: u8 = 2;
pub fn plan(s: &Snapshot) -> u8 {
    if s.busy == 0
        || s.has_epub == 0
        || s.load_popup != 0
        || s.remembered == 0
        || s.end_options != 0
        || s.touch != 0
        || s.remote != 0
        || s.home_action != 1
        || s.edges != 0
        || s.held_buttons != 0
        || s.spine < 0
        || s.spine >= s.spine_count
        || s.overlay != 0
        || s.automatic != 0
        || s.pending_turn != 0
        || s.bookmark_notice != 0
        || s.dictionary_notice != 0
        || s.font_notice != 0
        || s.clipping_notice != 0
        || s.read_move != 0
        || s.recents_removed != 0
        || s.applied_orientation != s.orientation
        || s.long_button != 0
        || s.short_power != 0
        || s.tilt != 0
        || s.confirm_threshold != 0
        || s.footnote_depth != 0
    {
        return 0;
    }
    ADMITTED
        | if s.touch_controls != 0 {
            SCREEN_LONG
        } else {
            0
        }
}
const _: () = {
    assert!(core::mem::size_of::<Snapshot>() == 40);
    assert!(core::mem::align_of::<Snapshot>() == 4);
    assert!(core::mem::offset_of!(Snapshot, spine) == 0);
    assert!(core::mem::offset_of!(Snapshot, spine_count) == 4);
    assert!(core::mem::offset_of!(Snapshot, confirm_threshold) == 8);
    assert!(core::mem::offset_of!(Snapshot, busy) == 12);
    assert!(core::mem::offset_of!(Snapshot, has_epub) == 13);
    assert!(core::mem::offset_of!(Snapshot, load_popup) == 14);
    assert!(core::mem::offset_of!(Snapshot, remembered) == 15);
    assert!(core::mem::offset_of!(Snapshot, end_options) == 16);
    assert!(core::mem::offset_of!(Snapshot, touch) == 17);
    assert!(core::mem::offset_of!(Snapshot, remote) == 18);
    assert!(core::mem::offset_of!(Snapshot, home_action) == 19);
    assert!(core::mem::offset_of!(Snapshot, edges) == 20);
    assert!(core::mem::offset_of!(Snapshot, held_buttons) == 21);
    assert!(core::mem::offset_of!(Snapshot, overlay) == 22);
    assert!(core::mem::offset_of!(Snapshot, automatic) == 23);
    assert!(core::mem::offset_of!(Snapshot, pending_turn) == 24);
    assert!(core::mem::offset_of!(Snapshot, bookmark_notice) == 25);
    assert!(core::mem::offset_of!(Snapshot, dictionary_notice) == 26);
    assert!(core::mem::offset_of!(Snapshot, font_notice) == 27);
    assert!(core::mem::offset_of!(Snapshot, clipping_notice) == 28);
    assert!(core::mem::offset_of!(Snapshot, read_move) == 29);
    assert!(core::mem::offset_of!(Snapshot, recents_removed) == 30);
    assert!(core::mem::offset_of!(Snapshot, applied_orientation) == 31);
    assert!(core::mem::offset_of!(Snapshot, orientation) == 32);
    assert!(core::mem::offset_of!(Snapshot, long_button) == 33);
    assert!(core::mem::offset_of!(Snapshot, short_power) == 34);
    assert!(core::mem::offset_of!(Snapshot, tilt) == 35);
    assert!(core::mem::offset_of!(Snapshot, footnote_depth) == 36);
    assert!(core::mem::offset_of!(Snapshot, touch_controls) == 37);
    assert!(core::mem::offset_of!(Snapshot, reserved) == 38);
};
#[cfg(test)]
mod tests {
    use super::*;
    fn idle() -> Snapshot {
        Snapshot {
            spine_count: 2,
            busy: 1,
            has_epub: 1,
            remembered: 1,
            home_action: 1,
            ..Snapshot::default()
        }
    }
    #[test]
    fn eligibility_excludes_edges_holds_deadlines_and_first_paint() {
        let s = idle();
        assert_eq!(plan(&s), ADMITTED);
        let variants = [
            Snapshot { busy: 0, ..s },
            Snapshot { has_epub: 0, ..s },
            Snapshot { load_popup: 1, ..s },
            Snapshot { remembered: 0, ..s },
            Snapshot {
                end_options: 1,
                ..s
            },
            Snapshot { touch: 1, ..s },
            Snapshot { remote: 1, ..s },
            Snapshot {
                home_action: 0,
                ..s
            },
            Snapshot { edges: 1, ..s },
            Snapshot {
                held_buttons: 1,
                ..s
            },
            Snapshot { spine: -1, ..s },
            Snapshot { spine: 2, ..s },
            Snapshot { overlay: 1, ..s },
            Snapshot { automatic: 1, ..s },
            Snapshot {
                pending_turn: -1,
                ..s
            },
            Snapshot {
                bookmark_notice: 1,
                ..s
            },
            Snapshot {
                dictionary_notice: 1,
                ..s
            },
            Snapshot {
                font_notice: 1,
                ..s
            },
            Snapshot {
                clipping_notice: 1,
                ..s
            },
            Snapshot { read_move: 1, ..s },
            Snapshot {
                recents_removed: 1,
                ..s
            },
            Snapshot {
                orientation: 1,
                ..s
            },
            Snapshot {
                long_button: 1,
                ..s
            },
            Snapshot {
                short_power: 1,
                ..s
            },
            Snapshot { tilt: 1, ..s },
            Snapshot {
                confirm_threshold: 400,
                ..s
            },
            Snapshot {
                footnote_depth: 1,
                ..s
            },
        ];
        for (index, variant) in variants.iter().enumerate() {
            assert_eq!(plan(variant), 0, "case {index}");
        }
        assert_eq!(
            plan(&Snapshot {
                touch_controls: 1,
                ..s
            }),
            ADMITTED | SCREEN_LONG
        );
    }
}
