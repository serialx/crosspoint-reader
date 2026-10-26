extern crate std;
use super::*;
use alloc::{rc::Rc, vec};
use core::cell::Cell;
#[cfg(feature = "chapter-trace")]
use core::cell::RefCell;

struct Payload {
    style: Block,
    words: u32,
    links: Vec<LineLink>,
    id: u32,
    drops: Rc<Cell<u32>>,
}
impl Drop for Payload {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}
struct MockParagraph {
    style: Block,
    count: u32,
    dropped: bool,
    suppressed: bool,
    reset: bool,
    lines: Vec<Line<Payload>>,
}
impl Paragraph<Payload> for MockParagraph {
    fn create(style: Block, _: Options) -> Result<Self> {
        Ok(Self {
            style,
            count: 0,
            dropped: false,
            suppressed: false,
            reset: false,
            lines: Vec::new(),
        })
    }
    fn style(&self) -> Block {
        self.style
    }
    fn set_style(&mut self, style: Block) {
        self.style = style;
    }
    fn size(&self) -> u32 {
        self.count
    }
    fn dropped(&self) -> bool {
        self.dropped
    }
    fn reset_indent(&mut self) {
        self.reset = true;
    }
    fn suppress_indent(&mut self) {
        self.suppressed = true;
    }
    fn prepare_font<S: Sink<Payload = Payload>>(
        &mut self,
        sink: &mut S,
        font: i32,
        scale: f32,
    ) -> i32 {
        let variant = sink.font_variant(font, scale);
        let selected = if variant == 0 { font } else { variant };
        self.style.font_id = if selected == font { 0 } else { selected };
        selected
    }
    fn layout<S: Sink<Payload = Payload>>(
        &mut self,
        sink: &mut S,
        layout: Layout,
        mut emit: impl FnMut(&mut S, Line<Payload>) -> Result,
    ) -> Result {
        if self.dropped {
            return Err(Error::Allocation);
        }
        let available = self.lines.len().saturating_sub(usize::from(!layout.last));
        let count = available.min(layout.max_lines as usize);
        for _ in 0..count {
            let mut line = self.lines.remove(0);
            line.payload.style = self.style;
            line.lines_after = self.lines.len() as u32;
            self.count = self.count.saturating_sub(line.payload.words);
            emit(sink, line)?;
        }
        Ok(())
    }
}
#[derive(Default)]
struct MockSink {
    pending_anchor: Vec<u8>,
    toc_anchors: Vec<Vec<u8>>,
    anchors: Vec<(Vec<u8>, u16)>,
    pages: Vec<(Page<Payload>, Position)>,
    stop: bool,
    fail: bool,
    image: Option<(u16, u16)>,
    metrics_calls: usize,
    drops: Rc<Cell<u32>>,
    #[cfg(feature = "chapter-trace")]
    trace_enabled: bool,
    #[cfg(feature = "chapter-trace")]
    observations: RefCell<Vec<std::string::String>>,
}
impl MockSink {
    fn line(&self, id: u32) -> Line<Payload> {
        Line {
            payload: Payload {
                id,
                drops: self.drops.clone(),
                style: Block::default(),
                words: 1,
                links: Vec::new(),
            },
            visible_offset: id * 10,
            lines_after: 1,
        }
    }
}
impl Sink for MockSink {
    type Payload = Payload;
    type Image = ();
    #[cfg(feature = "chapter-trace")]
    fn trace(&self, observation: trace::Observation<'_>) {
        if self.trace_enabled {
            self.observations
                .borrow_mut()
                .push(std::format!("{observation}"));
        }
    }
    fn pending_anchor(&self) -> &[u8] {
        &self.pending_anchor
    }
    fn defer_anchor(&mut self, text: &[u8]) -> Result {
        self.pending_anchor.clear();
        self.pending_anchor
            .try_reserve_exact(text.len())
            .map_err(|_| Error::Allocation)?;
        self.pending_anchor.extend_from_slice(text);
        Ok(())
    }
    fn anchor_starts_chapter(&self) -> bool {
        self.toc_anchors.contains(&self.pending_anchor)
    }
    fn record_anchor(&mut self, page: u16) -> Result {
        let id = core::mem::take(&mut self.pending_anchor);
        push(&mut self.anchors, (id, page))
    }
    fn font_variant(&mut self, font: i32, scale: f32) -> i32 {
        (font as f32 * scale) as i32
    }
    fn line_height(&mut self, font: i32, compression: f32) -> i32 {
        (font as f32 * compression) as i32
    }
    fn ascender(&mut self, font: i32) -> i32 {
        font * 3 / 4
    }
    fn space_width(&mut self, font: i32) -> i32 {
        font / 4
    }
    fn codepoint(&mut self, font: i32, _: u32, _: u8) -> Option<(i32, i32)> {
        Some((font * 8, font * 3 / 4))
    }
    fn line_metrics(&mut self, line: &Payload, _: Options) -> LineMetrics {
        self.metrics_calls += 1;
        LineMetrics {
            x: model::left(line.style),
            words: line.words,
            base_height: 10,
            ruby_shift: 0,
        }
    }
    fn set_line_margin(&mut self, line: &mut Payload, left: i16) {
        line.style.margin_left = left;
    }
    fn line_links<'a>(&'a mut self, line: &'a mut Payload) -> &'a [LineLink] {
        &line.links
    }
    fn complete(&mut self, page: Page<Payload>, position: Position) -> Result {
        if self.fail {
            return Err(Error::Allocation);
        }
        push(&mut self.pages, (page, position))?;
        if self.stop {
            Err(Error::Stopped)
        } else {
            Ok(())
        }
    }
    fn probe_image(&mut self, _: &[u8], counter: &mut i32, _: &mut bool) -> Result<ImageProbe<()>> {
        *counter += 1;
        Ok(self
            .image
            .map_or(ImageProbe::Unsupported, |(width, height)| {
                ImageProbe::Ready {
                    resource: (),
                    width,
                    height,
                }
            }))
    }
    fn create_image(&mut self, _: (), _: i32, _: i32) -> Result<Payload> {
        if self.fail {
            Err(Error::Allocation)
        } else {
            Ok(Payload {
                id: 999,
                drops: self.drops.clone(),
                style: Block::default(),
                words: 0,
                links: Vec::new(),
            })
        }
    }
}
fn options() -> Options {
    Options {
        font: 10,
        compression: 1.0,
        width: 100,
        height: 30,
        extra_spacing: false,
        alignment: 4,
        indent_spaces: 2,
        character_spacing: 0,
        word_spacing: 100,
        hyphenation: false,
        focus: false,
    }
}
type Asm = Assembly<MockParagraph, Payload>;
fn ids(page: &Page<Payload>) -> Vec<u32> {
    page.elements
        .iter()
        .filter_map(|e| match &e.content {
            Content::Line(p) => Some(p.id),
            _ => None,
        })
        .collect()
}
fn href(text: &[u8]) -> [u8; 256] {
    let mut value = [0; 256];
    value[..text.len()].copy_from_slice(text);
    value
}

#[test]
fn cpp_image_size_oracle() {
    let cases = include_str!("../../../../../../test/section_golden/goldens/assembly-images.txt");
    let mut count = 0;
    for line in cases.lines().filter(|line| !line.starts_with('#')) {
        let (input, output) = line.split_once(" => ").unwrap();
        let values: Vec<f32> = input
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let expected: Vec<i32> = output
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let flags = values[6] as u8;
        let css = ImageStyle {
            width: (flags & 1 != 0).then_some(Length {
                value: values[7],
                unit: values[8] as u8,
            }),
            height: (flags & 2 != 0).then_some(Length {
                value: values[9],
                unit: values[10] as u8,
            }),
        };
        assert_eq!(
            image_size(
                (values[0] as u16, values[1] as u16),
                (values[4] as i32, values[3] as u16),
                values[5],
                css
            ),
            (expected[0], expected[1]),
            "{line}"
        );
        count += 1;
    }
    assert_eq!(count, 420);
}

#[test]
fn first_page_zero_and_later_page_first_line_offsets() {
    let mut sink = MockSink::default();
    let mut asm = Asm::new(options());
    let flow = Flow::default();
    for id in 1..=4 {
        asm.add_line(sink.line(id), &flow, &mut sink).unwrap();
    }
    assert_eq!(sink.pages[0].1.visible, 0);
    assert_eq!(ids(&sink.pages[0].0), [1, 2, 3]);
    assert_eq!(asm.visible, 40);
    assert_eq!(asm.next_y, 10);
}

#[test]
fn carry_moves_links_and_copies_shared_footnote() {
    let mut sink = MockSink::default();
    let mut asm = Asm::new(options());
    let flow = Flow::default();
    for id in 1..=3 {
        let mut line = sink.line(id);
        line.payload.links.push(LineLink {
            href: href(b"#note"),
            x: 1,
            width: 4,
            top_lift: 0,
        });
        asm.add_line(line, &flow, &mut sink).unwrap();
    }
    asm.page
        .as_mut()
        .unwrap()
        .add_footnote(Footnote {
            href: href(b"#note"),
            ..Footnote::default()
        })
        .unwrap();
    asm.break_carrying(1, 1, 40, &flow, &mut sink).unwrap();
    let (old, _) = &sink.pages[0];
    assert_eq!(ids(old), [1, 2]);
    assert_eq!(old.links.len(), 2);
    assert_eq!(old.footnotes.len(), 1);
    let next = asm.page.as_ref().unwrap();
    assert_eq!(ids(next), [3]);
    assert_eq!(next.elements[0].y, 0);
    assert_eq!(next.links[0].y, 0);
    assert_eq!(next.footnotes.len(), 1);
    assert_eq!(asm.visible, 30);
    assert_eq!(asm.next_y, 10);
    assert_eq!(asm.paragraph_lines, 1);
}

#[test]
fn stop_drops_carried_and_incoming_lines_once() {
    let mut sink = MockSink::default();
    let drops = sink.drops.clone();
    let mut asm = Asm::new(options());
    let flow = Flow::default();
    for id in 1..=3 {
        asm.add_line(sink.line(id), &flow, &mut sink).unwrap();
    }
    asm.layout_active = true;
    asm.paragraph_lines = 1;
    sink.stop = true;
    assert_eq!(
        asm.add_line(sink.line(4), &flow, &mut sink),
        Err(Error::Stopped)
    );
    assert!(asm.stopped);
    assert_eq!(ids(&sink.pages[0].0), [1, 2]);
    assert_eq!(drops.get(), 2);
    asm.abort();
    drop(sink);
    assert_eq!(drops.get(), 4);
}

#[test]
fn sink_failure_consumes_page_and_latches_oom() {
    let mut sink = MockSink::default();
    let drops = sink.drops.clone();
    let mut asm = Asm::new(options());
    let flow = Flow::default();
    for id in 1..=3 {
        asm.add_line(sink.line(id), &flow, &mut sink).unwrap();
    }
    sink.fail = true;
    assert_eq!(
        asm.add_line(sink.line(4), &flow, &mut sink),
        Err(Error::Allocation)
    );
    assert!(asm.oom);
    assert_eq!(drops.get(), 4);
}

#[test]
fn carry_never_empties_page_or_crosses_decoration() {
    let mut sink = MockSink::default();
    let mut asm = Asm::new(options());
    let flow = Flow::default();
    asm.add_line(sink.line(1), &flow, &mut sink).unwrap();
    assert!(!asm.can_carry(1));
    asm.add_line(sink.line(2), &flow, &mut sink).unwrap();
    assert!(asm.can_carry(1));
    asm.add_element(Element {
        x: 0,
        y: 0,
        content: Content::Rule {
            width: 1,
            thickness: 1,
        },
    })
    .unwrap();
    assert!(!asm.can_carry(1));
}

#[test]
fn empty_block_merges_vertical_style_and_marker_reuse_keeps_inline_scale() {
    let mut sink = MockSink::default();
    let mut asm = Asm::new(options());
    let mut flow = Flow::default();
    asm.begin(&mut flow, &mut sink).unwrap();
    asm.paragraph.as_mut().unwrap().set_style(Block {
        margin_top: 7,
        padding_top: 2,
        break_before: 1,
        ..Block::default()
    });
    asm.start_block(
        Block {
            margin_top: 4,
            padding_top: 3,
            ..Block::default()
        },
        true,
        &mut flow,
        &mut sink,
    )
    .unwrap();
    let p = asm.paragraph.as_mut().unwrap();
    assert_eq!(p.style.margin_top, 7);
    assert_eq!(p.style.padding_top, 5);
    assert_eq!(p.style.break_before, 1);
    assert!(p.reset);
    p.count = 1;
    flow.marker_only = true;
    flow.inline_scale = Some(1.5);
    asm.start_block(Block::default(), true, &mut flow, &mut sink)
        .unwrap();
    assert!(!flow.marker_only);
    assert_eq!(flow.inline_scale, Some(1.5));
    assert!(sink.pages.is_empty());
}

#[test]
fn pending_break_consumed_even_on_empty_page() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink::default();
    let mut flow = Flow {
        page_break: true,
        ..Flow::default()
    };
    asm.begin(&mut flow, &mut sink).unwrap();
    asm.apply_page_break(&mut flow, &mut sink).unwrap();
    assert!(!flow.page_break);
    assert!(sink.pages.is_empty());
}

#[test]
fn boxes_slice_edges_and_preserve_nested_order() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink::default();
    let mut flow = Flow::default();
    let mut css = Css::default();
    for side in 0..4 {
        css.set(71 + side * 2, 1, Some(23 + side as u32));
        css.set(72 + side * 2, 1, None);
    }
    let mut style = Block::default();
    asm.open_box(&mut style, &css, &mut flow, &mut sink)
        .unwrap();
    assert_eq!(style.padding_top, 5);
    for id in 1..=4 {
        asm.add_line(sink.line(id), &flow, &mut sink).unwrap();
    }
    let old = sink.pages[0].0.elements.last().unwrap();
    match old.content {
        Content::Box { sides, height, .. } => {
            assert_eq!(sides[0].width, 1);
            assert_eq!(sides[2].width, 0);
            assert_eq!(height, 30);
        }
        _ => panic!(),
    }
    asm.close_box(&flow, &mut sink).unwrap();
    match asm.page.as_ref().unwrap().elements.last().unwrap().content {
        Content::Box { sides, .. } => {
            assert_eq!(sides[0].width, 0);
            assert_eq!(sides[2].width, 1);
        }
        _ => panic!(),
    }
}

#[test]
fn full_height_image_clamps_top_margin_and_resets_empty_style() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink {
        image: Some((100, 30)),
        ..MockSink::default()
    };
    let mut flow = Flow::default();
    asm.begin(&mut flow, &mut sink).unwrap();
    let p = asm.paragraph.as_mut().unwrap();
    p.style.margin_top = 8;
    assert_eq!(
        asm.process_image(
            b"image.png",
            ImageStyle::default(),
            &mut flow,
            &mut sink,
            |_, _, _| Ok(())
        ),
        Ok(ImageResult::Displayed)
    );
    assert_eq!(asm.page.as_ref().unwrap().elements[0].y, 0);
    assert_eq!(asm.next_y, 30);
    assert_eq!(asm.paragraph.as_ref().unwrap().style.margin_top, 0);
    assert_eq!(asm.image_counter, 1);
}

#[test]
fn table_fallback_is_bounded_and_dropped_cell_latches_oom() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink::default();
    let mut flow = Flow::default();
    for _ in 0..4 {
        asm.paragraph = Some(MockParagraph::create(Block::default(), options()).unwrap());
        flow.cell_open = true;
        asm.close_cell(&mut flow, &mut sink).unwrap();
    }
    assert_eq!(asm.cells.len(), 4);
    asm.paragraph = Some(MockParagraph {
        dropped: true,
        ..MockParagraph::create(Block::default(), options()).unwrap()
    });
    flow.cell_open = true;
    asm.close_cell(&mut flow, &mut sink).unwrap();
    assert!(asm.oom);
    assert!(asm.table_stacked);
    assert!(asm.cells.is_empty());
    assert!(asm.paragraph.is_none());
}

#[test]
fn toc_anchor_breaks_before_recording_page_index() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink::default();
    let flow = Flow::default();
    sink.toc_anchors.push(b"next".to_vec());
    asm.add_line(sink.line(1), &flow, &mut sink).unwrap();
    asm.defer_anchor(b"next", &mut sink).unwrap();
    asm.flush_anchor(&flow, &mut sink).unwrap();
    assert_eq!(sink.anchors, vec![(b"next".to_vec(), 1)]);
    assert_eq!(sink.pages.len(), 1);
}

#[test]
fn mock_layout_preserves_soft_flush_then_final_spacing() {
    let mut asm = Asm::new(Options {
        height: 100,
        extra_spacing: true,
        ..options()
    });
    let mut sink = MockSink::default();
    let mut flow = Flow::default();
    asm.begin(&mut flow, &mut sink).unwrap();
    let p = asm.paragraph.as_mut().unwrap();
    p.style.margin_top = 3;
    p.style.padding_top = 2;
    p.style.margin_bottom = 4;
    p.style.padding_bottom = 1;
    p.lines = (1..=3).map(|id| sink.line(id)).collect();
    p.count = 3;
    asm.make_pages(false, true, &mut flow, &mut sink).unwrap();
    assert_eq!(ids(asm.page.as_ref().unwrap()), [1, 2]);
    assert_eq!(asm.next_y, 25);
    asm.make_pages(true, true, &mut flow, &mut sink).unwrap();
    assert_eq!(ids(asm.page.as_ref().unwrap()), [1, 2, 3]);
    assert_eq!(asm.next_y, 45);
    assert_eq!(asm.extracted, 3);
}

#[test]
fn mocked_table_layout_aligns_rtl_columns_and_padding() {
    let mut asm = Asm::new(Options {
        height: 100,
        ..options()
    });
    let mut sink = MockSink::default();
    let mut flow = Flow {
        columns: 2,
        column_widths: [40, 60, 0, 0],
        ..Flow::default()
    };
    asm.table_rtl = true;
    for (column, padding) in [2, 5].into_iter().enumerate() {
        let mut p = MockParagraph::create(
            Block {
                padding_top: padding,
                padding_bottom: 3,
                padding_left: 4,
                ..Block::default()
            },
            options(),
        )
        .unwrap();
        p.lines.push(sink.line(column as u32 + 1));
        p.count = 1;
        asm.cells.push(p);
    }
    asm.finish_row(&mut flow, &mut sink).unwrap();
    let page = asm.page.as_ref().unwrap();
    assert_eq!(ids(page), [1, 2]);
    assert_eq!((page.elements[0].x, page.elements[0].y), (64, 2));
    assert_eq!((page.elements[1].x, page.elements[1].y), (4, 5));
    assert_eq!(asm.next_y, 22);
    assert!(asm.cells.is_empty());
    assert!(asm.cell_lines.iter().all(Vec::is_empty));
}

#[test]
fn mocked_drop_cap_uses_bounded_layout_then_restores_margin() {
    let mut asm = Asm::new(Options {
        height: 100,
        ..options()
    });
    let mut sink = MockSink::default();
    let mut flow = Flow::default();
    asm.begin(&mut flow, &mut sink).unwrap();
    flow.drop_cap.text[0] = b'A';
    flow.drop_cap.length = 1;
    flow.drop_cap.lines = 2;
    let p = asm.paragraph.as_mut().unwrap();
    p.count = 3;
    p.lines = (1..=3).map(|id| sink.line(id)).collect();
    asm.make_pages(true, true, &mut flow, &mut sink).unwrap();
    let page = asm.page.as_ref().unwrap();
    assert!(matches!(page.elements[0].content, Content::DropCap { .. }));
    assert_eq!(ids(page), [1, 2, 3]);
    assert!(page.elements[1].x > 0);
    assert_eq!(page.elements[3].x, 0);
    let p = asm.paragraph.as_ref().unwrap();
    assert_eq!(p.style.margin_left, 0);
    assert_eq!(p.style.text_indent_defined, 1);
    assert!(p.suppressed);
    assert_eq!(flow.drop_cap.length, 0);
}

std::thread_local! {
    static FAULT: Cell<Option<usize>> = const { Cell::new(None) };
    static CALLS: Cell<usize> = const { Cell::new(0) };
    static LIVE: Cell<isize> = const { Cell::new(0) };
}
struct FaultAllocator;
#[global_allocator]
static ALLOCATOR: FaultAllocator = FaultAllocator;
unsafe impl std::alloc::GlobalAlloc for FaultAllocator {
    unsafe fn alloc(&self, layout: std::alloc::Layout) -> *mut u8 {
        let active = FAULT.try_with(Cell::get).ok().flatten();
        if let Some(fail) = active {
            let index = CALLS.with(|calls| {
                let index = calls.get();
                calls.set(index + 1);
                index
            });
            if index == fail {
                return core::ptr::null_mut();
            }
        }
        let ptr = std::alloc::GlobalAlloc::alloc(&std::alloc::System, layout);
        if active.is_some() && !ptr.is_null() {
            LIVE.with(|live| live.set(live.get() + layout.size() as isize));
        }
        ptr
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: std::alloc::Layout) {
        if FAULT.try_with(Cell::get).ok().flatten().is_some() {
            LIVE.with(|live| live.set(live.get() - layout.size() as isize));
        }
        std::alloc::GlobalAlloc::dealloc(&std::alloc::System, ptr, layout);
    }
}

#[test]
fn allocation_failure_sweep_releases_page_payloads_and_buffers() {
    let mut successful = false;
    for fail in 0..256 {
        let mut sink = MockSink::default();
        let drops = sink.drops.clone();
        let mut created = 0;
        CALLS.with(|value| value.set(0));
        LIVE.with(|value| value.set(0));
        FAULT.with(|value| value.set(Some(fail)));
        let result = (|| {
            let mut asm = Asm::new(options());
            let mut flow = Flow::default();
            asm.begin(&mut flow, &mut sink)?;
            asm.defer_anchor(b"a-long-anchor-exceeding-inline-storage", &mut sink)?;
            for id in 1..=10 {
                let mut line = sink.line(id);
                created += 1;
                push(
                    &mut line.payload.links,
                    LineLink {
                        href: href(b"#note"),
                        x: 1,
                        width: 4,
                        top_lift: 0,
                    },
                )?;
                asm.add_footnote(
                    id as i32,
                    Footnote {
                        href: href(b"#note"),
                        ..Footnote::default()
                    },
                )?;
                asm.add_line(line, &flow, &mut sink)?;
            }
            asm.flush_anchor(&flow, &mut sink)?;
            asm.finish(&mut flow, &mut sink)
        })();
        drop(core::mem::take(&mut sink.pages));
        drop(core::mem::take(&mut sink.pending_anchor));
        drop(core::mem::take(&mut sink.anchors));
        FAULT.with(|value| value.set(None));
        assert_eq!(LIVE.with(Cell::get), 0, "failure index {fail}");
        assert_eq!(drops.get(), created, "failure index {fail}");
        if result.is_ok() {
            successful = true;
            break;
        }
        assert_eq!(result, Err(Error::Allocation), "failure index {fail}");
    }
    assert!(successful);
}

#[test]
fn page_strings_match_cpp_zero_padding() {
    let mut page = Page::<Payload>::default();
    let mut bytes = [0x55; 256];
    bytes[0] = b'a';
    bytes[1] = 0;
    page.add_link(Link {
        href: bytes,
        x: 0,
        y: 0,
        width: 1,
        height: 1,
    })
    .unwrap();
    let mut number = [0x55; 32];
    number[0] = 0;
    page.add_footnote(Footnote {
        href: bytes,
        number,
    })
    .unwrap();
    assert!(page.links[0].href[1..].iter().all(|b| *b == 0));
    assert!(page.footnotes[0].href[1..].iter().all(|b| *b == 0));
    assert!(page.footnotes[0].number.iter().all(|b| *b == 0));
}

#[cfg(feature = "chapter-trace")]
#[test]
fn observations_preserve_cpp_field_encoding_and_page_order() {
    let style = Block {
        character_spacing: -2,
        font_scale: 1.5,
        ..Block::default()
    };
    let fields = [
        trace::Field::from(&style),
        trace::Field::from(b"a\0z".as_slice()),
        trace::Field::from(1.5f32),
    ];
    assert_eq!(
        std::format!(
            "{}",
            trace::Observation {
                name: "Reply",
                fields: &fields
            }
        ),
        "Reply\t0,-2,0,0,0,0,0,0,0,0,0,0,0,0,0,1069547520,0,0,0,0,0\t61007a\t1069547520\n"
    );
    let mut asm = Asm::new(options());
    let mut sink = MockSink {
        trace_enabled: true,
        ..MockSink::default()
    };
    let flow = Flow {
        position: Position {
            paragraph: 2,
            list: 3,
            visible: 80,
            next_y: 0,
        },
        ..Flow::default()
    };
    for id in 1..=4 {
        asm.add_line(sink.line(id), &flow, &mut sink).unwrap();
    }
    assert_eq!(
        *sink.observations.borrow(),
        [
            "SetVisibleOffset\t10\n",
            "SetVisibleOffset\t20\n",
            "SetVisibleOffset\t30\n",
            "SetVisibleOffset\t40\n",
            "Page\t2\t3\t0\t30\n",
            "SetVisibleOffset\t40\n"
        ]
    );
}

#[test]
fn cpp_carry_reply_oracle() {
    let cases = include_str!("../../../../../../test/section_golden/goldens/assembly-carry.txt");
    let sink = MockSink::default();
    let mut count = 0;
    for line in cases.lines().filter(|line| !line.starts_with('#')) {
        let (input, output) = line.split_once(" => ").unwrap();
        let v: Vec<usize> = input
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let expected: Vec<usize> = output
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let mut asm = Asm::new(options());
        asm.layout_active = v[0] != 0;
        asm.layout_drop_cap = v[1] != 0;
        asm.paragraph_lines = v[2] as u8;
        asm.keep_lines = v[3] as u8;
        asm.lines_after = v[4] as u32;
        asm.recent_count = v[5];
        asm.ensure_page();
        for index in 0..v[6] {
            let content = if index + v[7] >= v[6] {
                Content::Line(sink.line(index as u32).payload)
            } else {
                Content::Rule {
                    width: 1,
                    thickness: 1,
                }
            };
            asm.add_element(Element {
                x: 0,
                y: 0,
                content,
            })
            .unwrap();
        }
        assert_eq!(asm.lines_to_carry(), (expected[0], expected[1]), "{line}");
        assert_eq!(asm.heading_carry(), expected[2], "{line}");
        count += 1;
    }
    assert_eq!(count, 2048);
}

#[test]
fn allocation_failures_release_buffered_table_and_carried_lines() {
    for table in [false, true] {
        let mut successful = false;
        for fail in 0..256 {
            let mut sink = MockSink::default();
            let drops = sink.drops.clone();
            let mut created = 0;
            CALLS.with(|v| v.set(0));
            LIVE.with(|v| v.set(0));
            FAULT.with(|v| v.set(Some(fail)));
            let result = (|| {
                let mut asm = Asm::new(options());
                let mut flow = Flow::default();
                if table {
                    flow.columns = 4;
                    flow.column_widths = [25; 4];
                    flow.table_border = Border { width: 1, style: 1 };
                    for col in 0..4 {
                        let mut p = MockParagraph::create(Block::default(), options())?;
                        p.count = 3;
                        for row in 0..3 {
                            created += 1;
                            push(&mut p.lines, sink.line(col * 3 + row))?;
                        }
                        push(&mut asm.cells, p)?;
                    }
                    asm.finish_row(&mut flow, &mut sink)?;
                } else {
                    for id in 1..=3 {
                        let mut line = sink.line(id);
                        created += 1;
                        push(
                            &mut line.payload.links,
                            LineLink {
                                href: href(b"#note"),
                                x: 1,
                                width: 4,
                                top_lift: 0,
                            },
                        )?;
                        asm.add_footnote(
                            id as i32,
                            Footnote {
                                href: href(b"#note"),
                                ..Footnote::default()
                            },
                        )?;
                        asm.add_line(line, &flow, &mut sink)?;
                    }
                    asm.layout_active = true;
                    asm.paragraph_lines = 1;
                    created += 1;
                    asm.add_line(sink.line(4), &flow, &mut sink)?;
                }
                Ok(())
            })();
            drop(core::mem::take(&mut sink.pages));
            drop(core::mem::take(&mut sink.pending_anchor));
            drop(core::mem::take(&mut sink.anchors));
            FAULT.with(|v| v.set(None));
            assert_eq!(LIVE.with(Cell::get), 0, "table={table}, fail={fail}");
            assert_eq!(drops.get(), created, "table={table}, fail={fail}");
            if result.is_ok() {
                successful = true;
                break;
            }
            assert_eq!(result, Err(Error::Allocation));
        }
        assert!(successful);
    }
}

#[test]
fn sink_owned_anchor_survives_finish_without_truncation() {
    let mut asm = Asm::new(options());
    let mut sink = MockSink::default();
    let mut flow = Flow::default();
    asm.begin(&mut flow, &mut sink).unwrap();
    let id = vec![b'x'; 300];
    asm.defer_anchor(&id, &mut sink).unwrap();
    asm.finish(&mut flow, &mut sink).unwrap();
    drop(asm);
    assert_eq!(sink.anchors, vec![(id, 0)]);
}
