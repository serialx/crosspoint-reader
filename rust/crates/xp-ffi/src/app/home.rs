extern crate alloc;

use super::{
    abi,
    cover_cache::CoverCache,
    home_books,
    home_text::{PreparedTexts, Texts},
    resources::{Bitmap, Buffer, Identity, Text},
    sync::ModelCell,
};
use alloc::vec::Vec;
use core::{cell::UnsafeCell, ffi::c_void};
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    ffi::{Paint, SceneView},
    home::{
        activity::{Geometry, InputResult, Model},
        grid,
        resource::{Resource, Text as TextResource},
        view, HomeState,
    },
    model::Input,
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
};

pub struct Home {
    identities: UnsafeCell<Vec<Identity>>,
    model: ModelCell<Model>,
    render: UnsafeCell<Frame>,
    services: &'static abi::Resources,
    runtime: &'static abi::Runtime,
    request_update: fn(),
    initial_menu: u8,
    clean_initial: bool,
}

struct Frame {
    commands: Vec<Command>,
    paint: PaintState,
}

struct PaintState {
    covers: Vec<Text>,
    grid_paths: [Text; 7],
    grid_cache: Option<CoverCache>,
    grid_height: i32,
    progress: i32,
    texts: PreparedTexts,
    layout: view::Layout,
    snapshot: HomeState,
    buffer: Option<Buffer>,
    cover_rect: Rect,
    restore_rect: Rect,
    cover_rendered: bool,
    buffer_stored: bool,
}

impl Default for PaintState {
    fn default() -> Self {
        Self {
            covers: Vec::new(),
            grid_paths: core::array::from_fn(|_| Text::default()),
            grid_cache: None,
            grid_height: 0,
            progress: -1,
            texts: PreparedTexts::new(-1, false),
            layout: view::Layout::default(),
            snapshot: HomeState::default(),
            buffer: None,
            cover_rect: Rect::default(),
            restore_rect: Rect::default(),
            cover_rendered: false,
            buffer_stored: false,
        }
    }
}

impl Home {
    pub fn new(
        services: &'static abi::Resources,
        runtime: &'static abi::Runtime,
        request_update: fn(),
        initial_menu: u8,
        clean_initial: bool,
    ) -> Self {
        Self {
            identities: UnsafeCell::new(Vec::new()),
            model: ModelCell::new(Model::default(), runtime),
            render: UnsafeCell::new(Frame {
                commands: Vec::new(),
                paint: PaintState::default(),
            }),
            services,
            runtime,
            request_update,
            initial_menu,
            clean_initial,
        }
    }

    /// Called once on the loop task; the model gates every early render.
    pub fn enter(&self) {
        unsafe { (self.runtime.diagnostic)(0, 0, 0) };
        let settings = unsafe { (self.services.settings_snapshot)() };
        let theme = self.theme();
        let limit = if settings.grid != 0 {
            7
        } else {
            usize::from(theme.columns).min(7)
        };
        let loaded = home_books::load(self.services, settings, limit);
        let mut state = HomeState::new(
            0,
            settings.grid != 0,
            settings.opds != 0,
            settings.plugins != 0,
            false,
        );
        // The render task cannot borrow these fields until complete_entry publishes readiness.
        let frame = unsafe { &mut *self.render.get() };
        if let Some(loaded) = loaded {
            state.books = loaded.identities.len() as u8;
            state.continuing = loaded.continuing;
            unsafe { *self.identities.get() = loaded.identities };
            frame.paint.covers = loaded.covers;
            frame.paint.progress = loaded.progress;
        } else {
            unsafe { (self.runtime.diagnostic)(2, 0, 0) };
        }
        frame.paint.texts = PreparedTexts::new(frame.paint.progress, settings.grid != 0);
        if settings.grid != 0 && state.books != 0 {
            frame.paint.grid_cache = Some(CoverCache::new(self.services, theme.screen));
        }
        state.initial_selection(self.initial_menu.into());
        self.model.with(|model| {
            model.complete_entry(state);
        });
        (self.request_update)();
    }

    pub fn exit(&self) {
        unsafe { (self.runtime.diagnostic)(1, 0, 0) };
    }

    pub fn input(&self, input: Input) -> InputResult {
        self.model.with(|model| model.input(input))
    }

    /// Entry is complete and the loop task retains this owner through the caller.
    pub unsafe fn path(&self, index: usize) -> Option<abi::Text> {
        unsafe { &*self.identities.get() }
            .get(index)
            .map(|identity| identity.path.wire())
    }

    fn theme(&self) -> abi::Theme {
        let mut theme = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.services.theme_snapshot)(theme.as_mut_ptr());
            theme.assume_init()
        }
    }

    fn reject(&self) {
        self.model.with(Model::reject);
        unsafe { (self.runtime.diagnostic)(3, 0, 0) };
    }

    /// The render task holds the lifetime lock. None defers an early notification;
    /// Some records a render attempt, matching the native painted-visit counter.
    pub unsafe fn render(&self, widths: &mut view::Widths) -> Option<bool> {
        let snapshot = self.model.with(Model::begin_render)?;
        let identities = unsafe { &*self.identities.get() };
        let frame = unsafe { &mut *self.render.get() };
        frame.paint.snapshot = snapshot;
        let Some(after) = self.paint_frame(frame, identities, widths) else {
            return Some(false);
        };
        if after == xp_ui::home::AfterPaint::LoadCovers {
            self.load_covers(frame, identities);
        }
        Some(true)
    }

    // Keep preparation locals off the native drawing and cover-loading call paths.
    #[inline(never)]
    fn prepare_scene(
        &self,
        frame: &mut Frame,
        identities: &[Identity],
        widths: &mut view::Widths,
        prepared_geometry: &mut Geometry,
    ) -> Option<usize> {
        let snapshot = frame.paint.snapshot;
        let theme = self.theme();
        frame.paint.texts.body_bold = theme.body_bold != 0;
        let prepared = if !snapshot.grid {
            frame.paint.layout = standard_layout(theme);
            let available = unsafe { (self.runtime.display_geometry)() }.frame_available != 0;
            frame.paint.prepare_covers(self.services, widths, available)
        } else {
            view::Prepared::default()
        };
        let (keys, count) = snapshot.menu_keys(frame.paint.layout.continue_in_menu != 0);
        frame.paint.texts.keys = keys;
        frame.paint.texts.count = count;
        let mut capacity = frame.commands.len().max(24);
        let (geometry, command_count) = loop {
            if frame.commands.len() < capacity {
                if frame
                    .commands
                    .try_reserve_exact(capacity - frame.commands.len())
                    .is_err()
                {
                    self.reject();
                    return None;
                }
                frame.commands.resize(capacity, Command::default());
            }
            let mut arena = match Arena::new(&mut frame.commands, &mut [], &mut []) {
                Ok(arena) => arena,
                Err(_) => {
                    self.reject();
                    return None;
                }
            };
            let mut texts = Texts {
                identities,
                prepared: &mut frame.paint.texts,
                services: self.services,
            };
            let geometry = if snapshot.grid {
                let layout = grid_layout(theme, frame.paint.progress, self.services);
                (|| {
                    arena.push(Command {
                        kind: Kind::Clear as u8,
                        ..Command::default()
                    })?;
                    let geometry = grid::view(&mut arena, snapshot, &layout, &mut texts)?;
                    arena.push(Command {
                        kind: Kind::Page as u8,
                        resource: Resource::Footer.encode(),
                        generation: 1,
                        ..Command::default()
                    })?;
                    Ok(Geometry::Grid(geometry))
                })()
            } else {
                view::standard(
                    &mut arena,
                    snapshot,
                    &frame.paint.layout,
                    &prepared,
                    &mut texts,
                )
                .map(Geometry::Standard)
            };
            match geometry {
                Ok(geometry) => match arena.finish(Refresh::None, 1) {
                    Ok(scene) => break (geometry, scene.commands.len()),
                    Err(_) => {
                        self.reject();
                        return None;
                    }
                },
                Err(Error::Capacity) if capacity < 128 => {
                    capacity = (capacity * 2).min(128);
                }
                Err(_) => {
                    self.reject();
                    return None;
                }
            }
        };
        *prepared_geometry = geometry;
        Some(command_count)
    }

    #[inline(never)]
    fn paint_frame(
        &self,
        frame: &mut Frame,
        identities: &[Identity],
        widths: &mut view::Widths,
    ) -> Option<xp_ui::home::AfterPaint> {
        let mut geometry = Geometry::Unpublished;
        let command_count = self.prepare_scene(frame, identities, widths, &mut geometry)?;
        let snapshot = frame.paint.snapshot;
        let mut changed = false;
        if let Geometry::Grid(geometry) = geometry {
            let display = unsafe { (self.runtime.display_geometry)() };
            if let Some(cache) = &mut frame.paint.grid_cache {
                cache.prepare(display.orientation);
            }
            changed = geometry.thumb_height > 0
                && frame.paint.grid_height != i32::from(geometry.thumb_height);
            if changed {
                frame.paint.grid_height = geometry.thumb_height.into();
                frame.paint.refresh_paths(self.services);
            }
        }
        let scene = SceneView {
            commands: frame.commands.as_ptr(),
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: command_count as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: 0,
            reserved: 0,
        };
        let mut context = PaintContext {
            state: &mut frame.paint,
            identities,
            services: self.services,
        };
        let resources = Paint {
            context: (&mut context as *mut PaintContext<'_>).cast(),
            clear: None,
            text: None,
            resource_valid: Some(PaintContext::valid),
            resource: Some(PaintContext::paint),
        };
        if unsafe { (self.services.paint_scene)(&scene, &resources) } == 0 {
            self.reject();
            return None;
        }
        if !self.model.with(|model| model.publish(geometry)) {
            return None;
        }
        let refresh = if self.clean_initial && !snapshot.first_render {
            Refresh::Half
        } else {
            Refresh::Fast
        };
        unsafe { (self.services.refresh)(refresh as u8) };
        if changed {
            // Paths already match this geometry; invalidate the first paint's snapshots.
            if let Some(cache) = &mut frame.paint.grid_cache {
                cache.invalidate();
            }
        }
        let after = self.model.with(|model| model.painted(changed));
        if after == xp_ui::home::AfterPaint::Repaint {
            (self.request_update)();
        }
        Some(after)
    }

    #[inline(never)]
    fn load_covers(&self, frame: &mut Frame, identities: &[Identity]) {
        let snapshot = frame.paint.snapshot;
        let height = if snapshot.grid {
            if frame.paint.grid_height > 0 {
                frame.paint.grid_height
            } else {
                400
            }
        } else {
            view::thumbnail_height(frame.paint.layout)
        };
        let rendered = &mut frame.paint.cover_rendered;
        let buffer = &mut frame.paint.buffer;
        let stored = &mut frame.paint.buffer_stored;
        home_books::load_covers(
            self.services,
            identities,
            &mut frame.paint.covers,
            height,
            snapshot.grid,
            || {
                // Generation invalidates this backup before the next paint.
                *buffer = None;
                *stored = false;
            },
            || {
                *rendered = false;
                (self.request_update)();
            },
        );
        self.model.with(Model::loaded);
        if snapshot.grid {
            frame.paint.refresh_paths(self.services);
            (self.request_update)();
        }
    }
}

impl PaintState {
    fn refresh_paths(&mut self, services: &'static abi::Resources) {
        if let Some(cache) = &mut self.grid_cache {
            cache.invalidate();
        }
        for (index, cover) in self.covers.iter().enumerate().take(7) {
            self.grid_paths[index] = if self.grid_height > 0 {
                home_books::thumbnail(services, cover, self.grid_height).unwrap_or_default()
            } else {
                Text::default()
            };
        }
    }

    fn prepare_covers(
        &mut self,
        services: &'static abi::Resources,
        widths: &mut view::Widths,
        frame_available: bool,
    ) -> view::Prepared {
        self.restore_rect = self.cover_rect;
        let restore_bytes = unsafe { (services.region_size)(self.cover_rect) };
        let mut prepared = view::Prepared {
            rendered: u8::from(self.cover_rendered),
            restored: u8::from(
                self.cover_rendered
                    && self.buffer_stored
                    && frame_available
                    && restore_bytes > 0
                    && self
                        .buffer
                        .as_ref()
                        .is_some_and(|b| b.len() >= restore_bytes),
            ),
            ..view::Prepared::default()
        };
        self.cover_rect = Rect {
            x: self.layout.content.x,
            y: self.layout.home_top,
            w: self.layout.content.w,
            h: self.layout.tile_height,
        };
        let height = view::thumbnail_height(self.layout);
        if !self.cover_rendered || self.layout.theme == 0 {
            for (index, cover) in self.covers.iter().take(3).enumerate() {
                if cover.is_empty() {
                    continue;
                }
                prepared.covers[index].status = 1;
                let Some(path) = home_books::thumbnail(services, cover, height) else {
                    continue;
                };
                if let Some(bitmap) = Bitmap::open(services, &path) {
                    let info = bitmap.info();
                    prepared.covers[index] = view::Cover {
                        width: info.width,
                        height: info.height,
                        status: if info.valid != 0 { 2 } else { 0 },
                    };
                }
            }
        }
        widths.prepare(self.snapshot, self.layout, &mut prepared);
        if !self.cover_rendered
            && !self.covers.is_empty()
            && (self.layout.theme != 0 || prepared.covers[0].status == 2)
        {
            self.buffer = None;
            self.buffer_stored = false;
            if self.cover_rect.w > 0 && self.cover_rect.h > 0 {
                let bytes = unsafe { (services.region_size)(self.cover_rect) };
                self.buffer = Buffer::allocate(services, abi::MEMORY_DEFAULT, bytes);
                prepared.can_snapshot = u8::from(self.buffer.is_some());
            }
        }
        prepared
    }
}

struct PaintContext<'a> {
    state: &'a mut PaintState,
    identities: &'a [Identity],
    services: &'static abi::Resources,
}

impl PaintContext<'_> {
    fn text(&mut self, handle: TextResource) -> Option<abi::Text> {
        Texts {
            identities: self.identities,
            prepared: &mut self.state.texts,
            services: self.services,
        }
        .lookup(handle)
    }

    unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
        let this = unsafe { &mut *context.cast::<Self>() };
        if generation != 1 {
            return 0;
        }
        let valid = match Resource::decode(handle) {
            Some(Resource::Text(text)) => this.text(text).is_some(),
            Some(Resource::Cover(index)) => usize::from(index) < this.identities.len(),
            Some(Resource::MenuIcon(index)) => usize::from(index) < this.state.texts.count,
            Some(Resource::GridCover(index)) => {
                this.state.snapshot.grid && usize::from(index) < this.identities.len()
            }
            Some(Resource::GridIcon(_)) => this.state.snapshot.grid,
            Some(_) => true,
            None => false,
        };
        u8::from(valid)
    }

    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &mut *context.cast::<Self>() };
        let command = unsafe { &*command };
        let Some(resource) = Resource::decode(command.resource) else {
            return;
        };
        if let Resource::Text(text) = resource {
            if let Some(text) = this.text(text) {
                unsafe { (this.services.text_draw)(command, clip, text) };
            }
            return;
        }
        let rect = command.rect;
        if this.state.snapshot.grid {
            if let Resource::GridCover(index) = resource {
                let index = usize::from(index);
                if let Some(cache) = &mut this.state.grid_cache {
                    cache.paint(index, rect, &this.state.grid_paths[index]);
                }
                return;
            }
            if let Resource::GridIcon(index) = resource {
                let icon = [1, 2, 3, 4, 5, 0][usize::from(index)];
                unsafe { (this.services.icon_draw)(icon, rect) };
                return;
            }
        }
        if let Resource::Cover(index) = resource {
            let index = usize::from(index);
            if let Some(path) = home_books::thumbnail(
                this.services,
                &this.state.covers[index],
                view::thumbnail_height(this.state.layout),
            ) {
                if let Some(mut bitmap) = Bitmap::open(this.services, &path) {
                    if bitmap.info().valid != 0 {
                        bitmap.draw(rect, 0);
                    }
                }
            }
            return;
        }
        if let Resource::MenuIcon(index) = resource {
            let key = this.state.texts.keys[usize::from(index)] as usize;
            unsafe { (this.services.icon_draw)([0, 1, 2, 3, 3, 4, 5][key], rect) };
            return;
        }
        match resource {
            Resource::Header => {
                let title = if !this.state.snapshot.grid && this.state.layout.continue_in_menu != 0
                {
                    this.identities.first().map(|book| book.title.wire())
                } else {
                    None
                };
                unsafe {
                    (this.services.header_draw)(
                        rect,
                        title.unwrap_or(abi::Text {
                            data: core::ptr::null(),
                            len: 0,
                        }),
                    )
                };
            }
            Resource::Footer => {
                let translate = this.services.translate;
                let labels = unsafe {
                    [
                        if this.state.snapshot.continuing {
                            translate(STR_RESUME)
                        } else {
                            abi::Text::from_bytes(&b"\0"[..0])
                        },
                        translate(STR_SELECT),
                        translate(if this.state.snapshot.grid {
                            STR_DIR_LEFT
                        } else {
                            STR_DIR_UP
                        }),
                        translate(if this.state.snapshot.grid {
                            STR_DIR_RIGHT
                        } else {
                            STR_DIR_DOWN
                        }),
                    ]
                };
                unsafe { (this.services.footer_draw)(labels.as_ptr()) };
            }
            Resource::Restore => {
                if let Some(buffer) = &this.state.buffer {
                    buffer.write(this.state.restore_rect, 0, buffer.len());
                }
            }
            Resource::Snapshot => {
                let stored = this
                    .state
                    .buffer
                    .as_mut()
                    .is_some_and(|buffer| buffer.read(this.state.cover_rect, 0, buffer.len()));
                if !stored {
                    this.state.buffer = None;
                }
                this.state.buffer_stored = stored;
                this.state.cover_rendered = stored;
            }
            Resource::MaskCorners => unsafe { (this.services.rounded_mask)(rect, 18, 2) },
            Resource::Ribbon => {
                let (x, y, w, h) = (
                    i32::from(rect.x),
                    i32::from(rect.y),
                    i32::from(rect.w),
                    i32::from(rect.h),
                );
                let points = [
                    abi::Point { x, y },
                    abi::Point { x: x + w, y },
                    abi::Point { x: x + w, y: y + h },
                    abi::Point {
                        x: x + w / 2,
                        y: y + h - h / 3,
                    },
                    abi::Point { x, y: y + h },
                ];
                unsafe {
                    (this.services.polygon_draw)(points.as_ptr(), 5, u8::from(command.paint == 1))
                };
            }
            Resource::Placeholder => unsafe { (this.services.placeholder_draw)(rect) },
            Resource::CoverIcon => unsafe { (this.services.icon_draw)(6, rect) },
            _ => {}
        }
    }
}

fn standard_layout(theme: abi::Theme) -> view::Layout {
    view::Layout {
        screen: theme.screen,
        content: theme.content,
        safe: theme.safe,
        title_font: theme.title_font,
        small_font: theme.small_font,
        tiny_font: theme.tiny_font,
        top_padding: theme.top_padding,
        header_height: theme.header_height,
        home_top: theme.home_top,
        tile_height: theme.tile_height,
        cover_height: theme.cover_height,
        side_padding: theme.side_padding,
        spacing: theme.spacing,
        menu_top: theme.menu_top,
        menu_height: theme.menu_height,
        menu_gap: theme.menu_gap,
        hints_height: theme.hints_height,
        scroll_width: theme.scroll_width,
        scroll_offset: theme.scroll_offset,
        theme: theme.theme,
        continue_in_menu: theme.continue_in_menu,
        columns: theme.columns,
    }
}

fn grid_layout(theme: abi::Theme, progress: i32, services: &abi::Resources) -> grid::Layout {
    grid::Layout {
        screen: theme.screen,
        safe: theme.safe,
        body_font: theme.body_font,
        small_font: theme.small_font,
        title_font: theme.title_font,
        progress,
        space_sm: (unsafe { (services.line_height)(theme.body_font) } / 6).max(4) as i16,
        space_lg: 16,
        header_side: theme.header_side,
        header_top: theme.top_padding,
        header_height: theme.battery_height,
        corner: theme.corner,
        tab_height: theme.tab_height,
        tab_height_pct: theme.tab_height_pct,
        tab_width_pct: theme.tab_width_pct,
        tab_offset: theme.tab_offset,
        touch: theme.touch,
        body_bold: theme.body_bold,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::boxed::Box;
    use core::sync::atomic::{AtomicU8, Ordering};
    use xp_ui::home::view::Texts as _;
    static WRAPS: AtomicU8 = AtomicU8::new(0);
    unsafe extern "C" fn wrap(
        text: abi::Text,
        _: abi::TextLayout,
        sink: abi::TextSink,
        context: *mut c_void,
    ) -> u8 {
        WRAPS.fetch_add(1, Ordering::Relaxed);
        let count = unsafe { text.bytes().unwrap() }[0] - b'0';
        for _ in 0..count {
            if unsafe { sink(context, abi::Text::from_bytes(b"line")) } == 0 {
                return 0;
            }
        }
        1
    }
    unsafe extern "C" fn width(_: abi::Text, _: i32, _: u8) -> i32 {
        4
    }

    #[test]
    fn validation_does_not_wrap() {
        let mut services = super::super::test_resources::resources();
        services.text_wrap = wrap;
        services.text_width = width;
        let services = Box::leak(Box::new(services));
        let identities: Vec<_> = [b"2", b"3", b"1"]
            .into_iter()
            .map(|title| Identity {
                path: Text::default(),
                title: Text::copy(title).unwrap(),
                author: Text::default(),
            })
            .collect();
        let mut state = PaintState::default();
        WRAPS.store(0, Ordering::Relaxed);
        for book in 0..3 {
            Texts {
                identities: &identities,
                prepared: &mut state.texts,
                services,
            }
            .wrap(TextResource::Title(book), 1, 40, 3, false)
            .unwrap();
        }
        assert_eq!(WRAPS.load(Ordering::Relaxed), 3);
        let mut context = PaintContext {
            state: &mut state,
            identities: &identities,
            services,
        };
        for (book, count) in [2, 3, 1].into_iter().enumerate() {
            for line in 0..4 {
                let handle = Resource::Text(TextResource::WrappedTitle {
                    book: book as u8,
                    line,
                })
                .encode();
                assert_eq!(
                    unsafe {
                        PaintContext::valid((&mut context as *mut PaintContext).cast(), handle, 1)
                    } != 0,
                    line < count
                );
                assert_eq!(
                    unsafe {
                        PaintContext::valid((&mut context as *mut PaintContext).cast(), handle, 2)
                    },
                    0
                );
            }
        }
        assert_eq!(
            WRAPS.load(Ordering::Relaxed),
            3,
            "validation must not wrap prepared titles"
        );
    }
    use core::sync::atomic::AtomicBool;
    use xp_ui::model::{BACK, CONFIRM, NAV_NEXT, POWER, TOUCH_HELD, TOUCH_PRESSED, TOUCH_RELEASED};
    static BUILD_OK: AtomicBool = AtomicBool::new(true);
    static PAINT_OK: AtomicBool = AtomicBool::new(true);
    static GRID_PAINT: AtomicBool = AtomicBool::new(false);
    static ENTERING: core::sync::atomic::AtomicPtr<Home> =
        core::sync::atomic::AtomicPtr::new(core::ptr::null_mut());
    unsafe extern "C" fn noop() {}
    unsafe extern "C" fn diagnostic(_: u8, _: u32, _: u32) {}
    unsafe extern "C" fn display_geometry() -> abi::DisplayGeometry {
        abi::DisplayGeometry {
            width: 480,
            height: 800,
            orientation: 0,
            frame_available: 0,
        }
    }
    unsafe extern "C" fn settings() -> abi::HomeSettings {
        abi::HomeSettings {
            grid: 0,
            plugins: 0,
            opds: 0,
            library_metadata: 0,
        }
    }
    unsafe extern "C" fn theme(out: *mut abi::Theme) {
        unsafe { *out = core::mem::zeroed() };
        let out = unsafe { &mut *out };
        out.screen = Rect {
            x: 0,
            y: 0,
            w: 480,
            h: 800,
        };
        out.content = Rect {
            x: 6,
            y: 0,
            w: 468,
            h: 800,
        };
        out.safe = out.screen;
        out.title_font = 12;
        out.small_font = 10;
        out.tiny_font = 8;
        out.body_font = 12;
        out.top_padding = 10;
        out.header_height = 50;
        out.home_top = 60;
        out.tile_height = 250;
        out.cover_height = 180;
        out.side_padding = 20;
        out.spacing = 10;
        out.menu_top = 12;
        out.menu_height = 42;
        out.menu_gap = 6;
        out.hints_height = 40;
        out.scroll_width = 4;
        out.scroll_offset = 6;
        out.columns = 3;
        out.tab_height = 40;
    }
    unsafe extern "C" fn recents(sink: abi::BookSink, context: *mut c_void) -> u8 {
        let empty = abi::Text::from_bytes(b"");
        for path in [b"/one".as_slice(), b"/two", b"/three"] {
            if let Some(home) = unsafe { ENTERING.load(Ordering::Relaxed).as_ref() } {
                assert_eq!(unsafe { home.render(&mut view::Widths::default()) }, None);
            }
            let book = abi::Book {
                path: abi::Text::from_bytes(path),
                title: empty,
                author: empty,
                cover: empty,
            };
            if unsafe { sink(context, &book) } == 0 {
                return 0;
            }
        }
        1
    }
    unsafe extern "C" fn measure(text: abi::Text, _: i32, _: u8) -> i32 {
        if BUILD_OK.load(Ordering::Relaxed) {
            text.len as i32 * 6
        } else {
            i32::MAX
        }
    }
    unsafe extern "C" fn line(_: i32) -> i32 {
        20
    }
    unsafe extern "C" fn translate(_: u16) -> abi::Text {
        abi::Text::from_bytes(b"label")
    }
    unsafe extern "C" fn truncate(
        text: abi::Text,
        _: abi::TextLayout,
        sink: abi::TextSink,
        context: *mut c_void,
    ) -> u8 {
        unsafe { sink(context, text) }
    }
    unsafe extern "C" fn region(_: Rect) -> usize {
        0
    }
    unsafe extern "C" fn paint(scene: *const SceneView, _: *const Paint) -> u8 {
        if GRID_PAINT.load(Ordering::Relaxed) {
            let scene = unsafe { &*scene };
            let commands =
                unsafe { core::slice::from_raw_parts(scene.commands, scene.command_count.into()) };
            assert_eq!(commands.first().unwrap().kind, Kind::Clear as u8);
            assert_eq!(commands.last().unwrap().kind, Kind::Page as u8);
            assert_eq!(commands.last().unwrap().resource, Resource::Footer.encode());
        }
        u8::from(PAINT_OK.load(Ordering::Relaxed))
    }
    unsafe extern "C" fn text_height(_: abi::Text, _: abi::TextLayout) -> i32 {
        20
    }
    unsafe extern "C" fn refresh(_: u8) {}
    fn fixture() -> Home {
        let mut services = super::super::test_resources::resources();
        services.settings_snapshot = settings;
        services.recents_visit = recents;
        services.theme_snapshot = theme;
        services.translate = translate;
        services.text_width = measure;
        services.line_height = line;
        services.text_wrap = truncate;
        services.text_truncate = truncate;
        services.region_size = region;
        services.text_box_height = text_height;
        services.paint_scene = paint;
        services.refresh = refresh;
        let mut runtime = super::super::test_resources::runtime();
        runtime.model_lock = noop;
        runtime.model_unlock = noop;
        runtime.diagnostic = diagnostic;
        runtime.display_geometry = display_geometry;
        Home::new(
            Box::leak(Box::new(services)),
            Box::leak(Box::new(runtime)),
            || {},
            0,
            false,
        )
    }

    #[test]
    fn family_build_and_paint_rejection() {
        for first in [false, true] {
            for build_failure in [false, true] {
                BUILD_OK.store(true, Ordering::Relaxed);
                PAINT_OK.store(true, Ordering::Relaxed);
                let home = fixture();
                ENTERING.store((&home as *const Home).cast_mut(), Ordering::Relaxed);
                home.enter();
                ENTERING.store(core::ptr::null_mut(), Ordering::Relaxed);
                let mut widths = view::Widths::default();
                let mut updates = 0;
                let mut effects = 0;
                let dispatch = |input, updates: &mut u32, effects: &mut u32| {
                    if let InputResult::Update(update) = home.input(input) {
                        *updates += 1;
                        *effects += u32::from(update.action.is_some());
                    }
                };
                dispatch(
                    Input {
                        released: CONFIRM,
                        ..Input::default()
                    },
                    &mut updates,
                    &mut effects,
                );
                assert_eq!(updates, 1, "pre-first-paint input remains live");
                if first {
                    assert_eq!(unsafe { home.render(&mut widths) }, Some(true));
                }
                BUILD_OK.store(!build_failure, Ordering::Relaxed);
                PAINT_OK.store(build_failure, Ordering::Relaxed);
                assert_eq!(unsafe { home.render(&mut widths) }, Some(false));
                let before = updates;
                let before_effects = effects;
                for button in [CONFIRM, NAV_NEXT, POWER] {
                    dispatch(
                        Input {
                            pressed: button,
                            held: button,
                            held_ms: 1001,
                            ..Input::default()
                        },
                        &mut updates,
                        &mut effects,
                    );
                    dispatch(
                        Input {
                            released: button,
                            held_ms: 1001,
                            ..Input::default()
                        },
                        &mut updates,
                        &mut effects,
                    );
                }
                dispatch(
                    Input {
                        touch_flags: TOUCH_PRESSED | TOUCH_HELD,
                        ..Input::default()
                    },
                    &mut updates,
                    &mut effects,
                );
                dispatch(
                    Input {
                        touch_flags: TOUCH_RELEASED,
                        ..Input::default()
                    },
                    &mut updates,
                    &mut effects,
                );
                assert_eq!((updates, effects), (before, before_effects));
                dispatch(
                    Input {
                        released: BACK,
                        ..Input::default()
                    },
                    &mut updates,
                    &mut effects,
                );
                assert_eq!(
                    effects, before_effects,
                    "Home Back must not act while rejected"
                );
                BUILD_OK.store(true, Ordering::Relaxed);
                PAINT_OK.store(true, Ordering::Relaxed);
                assert_eq!(unsafe { home.render(&mut widths) }, Some(true));
                dispatch(
                    Input {
                        released: CONFIRM,
                        ..Input::default()
                    },
                    &mut updates,
                    &mut effects,
                );
                assert_eq!(updates, before + 1);
            }
        }
        let home = fixture();
        home.model
            .with(|model| model.complete_entry(HomeState::new(0, true, false, false, false)));
        GRID_PAINT.store(true, Ordering::Relaxed);
        assert_eq!(
            unsafe { home.render(&mut view::Widths::default()) },
            Some(true)
        );
        GRID_PAINT.store(false, Ordering::Relaxed);
    }
}

#[cfg(test)]
mod backup_lifetime_tests {
    use super::*;
    use alloc::{boxed::Box, vec};
    use core::sync::atomic::{AtomicBool, AtomicU8, Ordering::Relaxed};

    static MODE: AtomicU8 = AtomicU8::new(0);
    static RELEASED: AtomicBool = AtomicBool::new(false);
    static GENERATED: AtomicBool = AtomicBool::new(false);
    static UPDATED: AtomicBool = AtomicBool::new(false);
    static RECENT_CLEARED: AtomicBool = AtomicBool::new(false);
    unsafe extern "C" fn noop() {}
    unsafe extern "C" fn allocate(_: u8, _: usize) -> *mut u8 {
        core::ptr::NonNull::<u8>::dangling().as_ptr()
    }
    unsafe extern "C" fn release(_: u8, _: *mut u8) {
        assert!(!GENERATED.load(Relaxed));
        RELEASED.store(true, Relaxed);
    }
    unsafe extern "C" fn exists(_: abi::Text) -> u8 {
        u8::from(MODE.load(Relaxed) == 0)
    }
    unsafe extern "C" fn thumbnail(
        _: abi::Text,
        _: i32,
        sink: abi::TextSink,
        context: *mut c_void,
    ) -> u8 {
        unsafe { sink(context, abi::Text::from_bytes(b"cover.bmp")) }
    }
    unsafe extern "C" fn open(_: u8, _: abi::Text) -> *mut c_void {
        if MODE.load(Relaxed) == 1 {
            core::ptr::null_mut()
        } else {
            core::ptr::NonNull::<u8>::dangling().as_ptr().cast()
        }
    }
    unsafe extern "C" fn load(_: *mut c_void, _: u8, _: u8) -> u8 {
        u8::from(MODE.load(Relaxed) != 2)
    }
    unsafe extern "C" fn generate(_: *mut c_void, _: i32) -> u8 {
        assert!(RELEASED.load(Relaxed));
        assert!(!UPDATED.load(Relaxed));
        GENERATED.store(true, Relaxed);
        u8::from(MODE.load(Relaxed) != 3)
    }
    unsafe extern "C" fn close(_: *mut c_void) {}
    unsafe extern "C" fn translate(_: u16) -> abi::Text {
        abi::Text::from_bytes(b"Loading")
    }
    unsafe extern "C" fn popup(_: abi::Text) -> Rect {
        assert!(!RELEASED.load(Relaxed));
        Rect::default()
    }
    unsafe extern "C" fn progress(_: Rect, _: i32) {
        assert!(!RELEASED.load(Relaxed));
    }
    unsafe extern "C" fn recent(book: *const abi::Book) {
        assert!(GENERATED.load(Relaxed));
        assert_eq!(unsafe { (*book).cover.bytes() }, Some(&b""[..]));
        RECENT_CLEARED.store(true, Relaxed);
    }
    fn update() {
        assert!(GENERATED.load(Relaxed));
        UPDATED.store(true, Relaxed);
    }

    #[test]
    fn backup_survives_skips_but_leaves_before_generation() {
        let mut services = super::super::test_resources::resources();
        services.buffer_allocate = allocate;
        services.buffer_release = release;
        services.path_exists = exists;
        services.thumbnail_path = thumbnail;
        services.book_open = open;
        services.book_load = load;
        services.book_generate_thumb = generate;
        services.book_close = close;
        services.translate = translate;
        services.popup_draw = popup;
        services.popup_progress = progress;
        services.recent_update = recent;
        // Unexpected region reads/writes retain the panic leaves: no popup restore.
        let services = Box::leak(Box::new(services));
        let mut runtime = super::super::test_resources::runtime();
        runtime.model_lock = noop;
        runtime.model_unlock = noop;
        let runtime = Box::leak(Box::new(runtime));
        for mode in 0..5 {
            MODE.store(mode, Relaxed);
            RELEASED.store(false, Relaxed);
            GENERATED.store(false, Relaxed);
            UPDATED.store(false, Relaxed);
            RECENT_CLEARED.store(false, Relaxed);
            let home = Home::new(services, runtime, update, 0, false);
            let identities = [Identity {
                path: Text::copy(if mode == 2 {
                    b"/book.xtc"
                } else {
                    b"/book.epub"
                })
                .unwrap(),
                title: Text::default(),
                author: Text::default(),
            }];
            let mut frame = Frame {
                commands: Vec::new(),
                paint: PaintState {
                    covers: vec![Text::copy(b"cover").unwrap()],
                    buffer: Buffer::allocate(services, abi::MEMORY_DEFAULT, 14694),
                    buffer_stored: true,
                    cover_rendered: true,
                    ..PaintState::default()
                },
            };
            home.load_covers(&mut frame, &identities);
            let generated = mode >= 3;
            assert_eq!(GENERATED.load(Relaxed), generated);
            assert_eq!(RELEASED.load(Relaxed), generated);
            assert_eq!(UPDATED.load(Relaxed), generated);
            assert_eq!(frame.paint.buffer.is_none(), generated);
            assert_eq!(frame.paint.buffer_stored, !generated);
            assert_eq!(frame.paint.cover_rendered, !generated);
            assert_eq!(frame.paint.covers[0].is_empty(), mode == 3);
            assert_eq!(RECENT_CLEARED.load(Relaxed), mode == 3);
            GENERATED.store(false, Relaxed);
        }
    }
}
