#[test]
fn definition_body_uses_initialized_layout_and_no_header_query() {
    unsafe extern "C" fn layout(
        out: *mut xp_ui::dictionary::definition::Layout,
        x: i32,
        y: i32,
    ) -> u8 {
        assert_eq!((x, y), (-1, -1));
        unsafe {
            out.write(xp_ui::dictionary::definition::Layout {
                screen: xp_ui::scene::Rect {
                    x: 0,
                    y: 0,
                    w: 480,
                    h: 800,
                },
                left: 13,
                right: 17,
                top: 19,
                bottom: 23,
                side_padding: 7,
                top_padding: 5,
                header_height: 41,
                spacing: 3,
                hints_height: 33,
                ..Default::default()
            })
        };
        1
    }
    let mut physical = crate::definition_physical::DefinitionPhysical::unused();
    physical.layout = layout;
    let body = unsafe { physical.body() };
    assert_eq!((body.x, body.y, body.w, body.h), (20, 68, 436, 696));
}
