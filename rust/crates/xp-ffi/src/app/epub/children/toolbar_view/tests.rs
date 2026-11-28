use super::*;
use core::cell::Cell;
use xp_ui::reader_chrome::list::Resources as _;

#[test]
fn toolbar_window_is_bounded_reuses_text_and_never_exposes_failed_fill() {
    let services = crate::app::test_resources::resources();
    let content = Content {
        panel: true,
        dense: false,
        active_tool: 1,
        progress_permille: 500,
        chapter: None,
        page_info: None,
        panel_title: None,
        count: 1000,
        selected: 50,
    };
    let mut storage = core::array::from_fn(|_| ToolbarRow::default());
    let (mut first, mut count, mut indicator) = (0, 0, [0; 16]);
    let phase = Cell::new(0);
    let mut fill = |index, row: &mut ToolbarRow| {
        if phase.get() == 2 && index == 89 {
            return false;
        }
        assert!(phase.get() != 3, "invalid window must not fill");
        row.toggle = true;
        row.checked = index % 2 == 0;
        row.label
            .assign(if phase.get() == 0 {
                b"retained label"
            } else {
                b"short"
            })
            .is_ok()
            && row.value.assign(b"retained value").is_ok()
    };
    let mut rows = Rows {
        services: &services,
        content: &content,
        fill: &mut fill,
        rows: &mut storage,
        first: &mut first,
        count: &mut count,
        indicator: &mut indicator,
    };
    rows.window(50, 16).unwrap();
    let pointer = rows.row_ref(50).unwrap().label.bytes().as_ptr();
    assert!(rows.row(50).is_ok());
    phase.set(1);
    rows.window(66, 16).unwrap();
    assert_eq!(rows.row_ref(66).unwrap().label.bytes().as_ptr(), pointer);
    assert!(rows.row(65).is_err() && rows.row(82).is_err());
    phase.set(3);
    assert!(rows.window(82, 17).is_err());
    assert_eq!(*rows.count, 0);
    assert!(rows.row(66).is_err());
    assert!(rows.window(999, 2).is_err());
    phase.set(2);
    assert!(rows.window(82, 16).is_err());
    assert_eq!(*rows.count, 0);
    assert!(rows.row(82).is_err());
}
