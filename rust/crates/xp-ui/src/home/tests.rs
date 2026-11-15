use super::*;
use crate::{
    model::{self, Input},
    reader_chrome::hits::Control,
    scene::{HitSink, Interaction, Rect},
};

#[test]
fn malformed_published_grid_never_divides_by_zero_or_wraps_counts() {
    for books in [0, 1, 7, 8, 255] {
        for tabs in [0, 1, 2, 6, 255] {
            let g = grid::Geometry {
                books,
                tab_count: tabs,
                tab_distributed: 1,
                screen: Rect {
                    x: 0,
                    y: 0,
                    w: 528,
                    h: 792,
                },
                ..grid::Geometry::default()
            };
            if books > 7 || !(2..=6).contains(&tabs) {
                assert_eq!(g.hit(1, 1), -1);
            }
        }
    }
}
#[test]
fn header_append_is_transactional_and_controls_override_rows() {
    let mut h = hits::Layout::default();
    h.push(Interaction {
        rect: Rect {
            x: 0,
            y: 0,
            w: 100,
            h: 50,
        },
        action: 1,
        value: 4,
        generation: 1,
        inputs: 1,
        flags: 0,
    })
    .unwrap();
    let c = Control {
        rect: Rect {
            x: 0,
            y: 0,
            w: 20,
            h: 20,
        },
        action: 4,
        value: 0,
        reserved: [0; 3],
    };
    let invalid = Control { action: 7, ..c };
    assert!(h.header(&[c, invalid]).is_err());
    assert_eq!(h.control_count, 0);
    assert_eq!(h.hit(1, 1).unwrap().action, 1);
    h.header(&[c]).unwrap();
    assert_eq!(h.hit(1, 1).unwrap().action, 4);
    assert_eq!(h.hit(30, 30).unwrap().value, 4);
}
#[test]
fn library_sort_keeps_live_resource_order_until_binding_transaction() {
    let mut s = library::State::default();
    s.lists[0].top = 3;
    let before = s;
    let u = s.update_bound(
        Input {
            released: model::CONFIRM,
            ..Input::default()
        },
        library::Facts {
            count: 5,
            books: 5,
            ..library::Facts::default()
        },
    );
    assert_eq!(u.effect.kind, library::SORT);
    assert_eq!(u.effect.invalidate, 0);
    assert_eq!(s.active_tab, before.active_tab);
    assert_eq!(s.sort, before.sort);
    s.event(5, u.effect.value, 0);
    assert_eq!(s.active_tab, 0);
    assert_eq!(s.sort, 2);
    s.event(6, u.effect.value, 0);
    assert_eq!(s.active_tab, 1);
    assert_eq!(s.nav().selected, 0);
}
#[test]
fn query_result_focus_and_group_restore_preserve_native_rules() {
    let mut s = library::State::default();
    s.event(3, 1, 0);
    s.event(4, 0, 0);
    assert_eq!(s.nav().selected, 0);
    s.event(4, 0, 3);
    assert_eq!(s.nav().selected, 1);
    s.nav_mut().top = 8;
    s.nav_mut().selected = 10;
    s.event(8, 2, 0);
    assert_eq!(s.nav().selected, 3);
    assert_eq!(s.collapsed, 1);
    s.event(9, -1, 0);
    assert_eq!(s.nav().selected, 10);
    assert_eq!(s.nav().top, 8);
}
#[test]
fn file_directory_transition_waits_for_loaded_resource_set() {
    let mut s = file::State::default();
    s.list.selected = 3;
    s.list.top = 2;
    let u = s.update_bound(
        Input {
            released: model::CONFIRM,
            ..Input::default()
        },
        file::Facts {
            count: 5,
            directory: 1,
            ..file::Facts::default()
        },
    );
    assert_eq!(u.effect.kind, file::DIRECTORY);
    assert_eq!(u.effect.invalidate, 0);
    assert_eq!(s.list.selected, 3);
    assert_eq!(s.list.top, 2);
    s.loaded(file::DIRECTORY, 0, 4);
    assert_eq!(s.list.selected, 0);
    assert_eq!(s.list.top, 0);
}
#[test]
fn null_view_boundaries_do_not_publish_outputs() {
    let mut scene = crate::ffi::SceneView {
        generation: 77,
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        command_count: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    let mut feedback = file_view::Feedback {
        path_start: 123,
        ..file_view::Feedback::default()
    };
    let mut hits = hits::Layout {
        first_index: 456,
        ..hits::Layout::default()
    };
    let result = unsafe {
        list_ffi::file_view(
            core::ptr::null(),
            core::ptr::null(),
            core::ptr::null(),
            core::ptr::null(),
            core::ptr::null(),
            &mut feedback,
            &mut hits,
            &mut scene,
        )
    };
    assert_eq!(result, 0);
    assert_eq!(feedback.path_start, 123);
    assert_eq!(hits.first_index, 456);
    assert_eq!(scene.generation, 77);
}
