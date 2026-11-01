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
        grid, view, HomeState,
    },
    model::Input,
    scene::{Arena, Command, Error, Rect, Refresh},
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
                paint: PaintState {
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
                },
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
            state.continuing = u8::from(loaded.continuing);
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
        let theme = self.theme();
        frame.paint.texts.body_bold = theme.body_bold != 0;
        let prepared = if snapshot.grid == 0 {
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
                    return Some(false);
                }
                frame.commands.resize(capacity, Command::default());
            }
            let mut arena = match Arena::new(&mut frame.commands, &mut [], &mut []) {
                Ok(arena) => arena,
                Err(_) => {
                    self.reject();
                    return Some(false);
                }
            };
            let mut texts = Texts {
                identities,
                prepared: &mut frame.paint.texts,
                services: self.services,
            };
            let geometry = if snapshot.grid != 0 {
                let layout = grid_layout(theme, frame.paint.progress, self.services);
                grid::view(&mut arena, snapshot, &layout, &mut texts).map(Geometry::Grid)
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
                        return Some(false);
                    }
                },
                Err(Error::Capacity) if capacity < 128 => {
                    capacity = (capacity + 4).min(128);
                }
                Err(_) => {
                    self.reject();
                    return Some(false);
                }
            }
        };
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
            return Some(false);
        }
        if !self.model.with(|model| model.publish(geometry)) {
            return Some(false);
        }
        let refresh = if self.clean_initial && snapshot.first_render == 0 {
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
        if after.invalidate != 0 {
            (self.request_update)();
        }
        if after.kind == 4 {
            let height = if snapshot.grid != 0 {
                if frame.paint.grid_height > 0 {
                    frame.paint.grid_height
                } else {
                    400
                }
            } else {
                view::thumbnail_height(frame.paint.layout)
            };
            let rendered = &mut frame.paint.cover_rendered;
            home_books::load_covers(
                self.services,
                identities,
                &mut frame.paint.covers,
                height,
                snapshot.grid != 0,
                || {
                    *rendered = false;
                    (self.request_update)();
                },
            );
            self.model.with(Model::loaded);
            if snapshot.grid != 0 {
                frame.paint.refresh_paths(self.services);
                (self.request_update)();
            }
        }
        Some(true)
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
                        reserved: [0; 3],
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
    fn text(&mut self, handle: u32) -> Option<abi::Text> {
        Texts {
            identities: self.identities,
            prepared: &mut self.state.texts,
            services: self.services,
        }
        .lookup(handle)
    }

    unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
        let this = unsafe { &mut *context.cast::<Self>() };
        u8::from(
            generation == 1
                && (this.text(handle).is_some()
                    || (view::HEADER..=view::COVER_ICON).contains(&handle)
                    || (view::COVER..view::COVER + this.identities.len() as u32).contains(&handle)
                    || (view::MENU_ICON..view::MENU_ICON + this.state.texts.count as u32)
                        .contains(&handle)
                    || this.state.snapshot.grid != 0
                        && ((grid::GRID_COVER..grid::GRID_COVER + this.identities.len() as u32)
                            .contains(&handle)
                            || (grid::GRID_ICON..grid::GRID_ICON + 6).contains(&handle))),
        )
    }

    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &mut *context.cast::<Self>() };
        let command = unsafe { &*command };
        let handle = command.resource;
        if let Some(text) = this.text(handle) {
            unsafe { (this.services.text_draw)(command, clip, text) };
            return;
        }
        let rect = command.rect;
        if this.state.snapshot.grid != 0 {
            if (grid::GRID_COVER..grid::GRID_COVER + this.identities.len() as u32).contains(&handle)
            {
                let index = (handle - grid::GRID_COVER) as usize;
                if let Some(cache) = &mut this.state.grid_cache {
                    cache.paint(index, rect, &this.state.grid_paths[index]);
                }
                return;
            }
            if (grid::GRID_ICON..grid::GRID_ICON + 6).contains(&handle) {
                let icon = [1, 2, 3, 4, 5, 0][(handle - grid::GRID_ICON) as usize];
                unsafe { (this.services.icon_draw)(icon, rect) };
                return;
            }
        }
        if (view::COVER..view::COVER + this.identities.len() as u32).contains(&handle) {
            let index = (handle - view::COVER) as usize;
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
        if (view::MENU_ICON..view::MENU_ICON + this.state.texts.count as u32).contains(&handle) {
            let key = this.state.texts.keys[(handle - view::MENU_ICON) as usize] as usize;
            unsafe { (this.services.icon_draw)([0, 1, 2, 3, 3, 4, 5][key], rect) };
            return;
        }
        match handle {
            view::HEADER => {
                let title =
                    if this.state.snapshot.grid == 0 && this.state.layout.continue_in_menu != 0 {
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
            view::FOOTER => {
                let translate = this.services.translate;
                let labels = unsafe {
                    [
                        if this.state.snapshot.continuing != 0 {
                            translate(STR_RESUME)
                        } else {
                            abi::Text::from_bytes(&b"\0"[..0])
                        },
                        translate(STR_SELECT),
                        translate(if this.state.snapshot.grid != 0 {
                            STR_DIR_LEFT
                        } else {
                            STR_DIR_UP
                        }),
                        translate(if this.state.snapshot.grid != 0 {
                            STR_DIR_RIGHT
                        } else {
                            STR_DIR_DOWN
                        }),
                    ]
                };
                unsafe { (this.services.footer_draw)(labels.as_ptr()) };
            }
            view::RESTORE => {
                if let Some(buffer) = &this.state.buffer {
                    buffer.write(this.state.restore_rect, 0, buffer.len());
                }
            }
            view::SNAPSHOT => {
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
            view::MASK_CORNERS => unsafe { (this.services.rounded_mask)(rect, 18, 2) },
            view::RIBBON => {
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
            view::PLACEHOLDER => unsafe { (this.services.placeholder_draw)(rect) },
            view::COVER_ICON => unsafe { (this.services.icon_draw)(6, rect) },
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
        reserved: 0,
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
        reserved: [0; 2],
    }
}
