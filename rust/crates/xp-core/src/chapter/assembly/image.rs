use super::*;

#[derive(Clone, Copy, Default)]
pub struct Length {
    pub value: f32,
    pub unit: u8,
}
impl Length {
    fn pixels(self, em: f32, container: f32) -> f32 {
        match self.unit {
            1 | 2 => self.value * em,
            3 => self.value * 1.33,
            4 => self.value * container / 100.0,
            _ => self.value,
        }
    }
}
#[derive(Clone, Copy, Default)]
pub struct ImageStyle {
    pub width: Option<Length>,
    pub height: Option<Length>,
}
impl ImageStyle {
    pub fn from_css(css: &Css) -> Self {
        Self {
            width: css.has(14).then(|| Length {
                value: css.value(55),
                unit: css.unit(55),
            }),
            height: css.has(13).then(|| Length {
                value: css.value(50),
                unit: css.unit(50),
            }),
        }
    }
}

pub fn image_size(
    intrinsic: (u16, u16),
    container: (i32, u16),
    em: f32,
    css: ImageStyle,
) -> (i32, i32) {
    let (width, height) = intrinsic;
    let (container_width, viewport_height) = (container.0, i32::from(container.1));
    let valid = width > 0 && height > 0;
    let (mut display_width, mut display_height);
    match (css.width, css.height, valid) {
        (Some(w), Some(h), true) => {
            display_height = ((h.pixels(em, viewport_height as f32) + 0.5) as i32).max(1);
            display_width = ((w.pixels(em, container_width as f32) + 0.5) as i32).max(1);
            if display_width > container_width || display_height > viewport_height {
                let sx = if display_width > container_width {
                    container_width as f32 / display_width as f32
                } else {
                    1.0
                };
                let sy = if display_height > viewport_height {
                    viewport_height as f32 / display_height as f32
                } else {
                    1.0
                };
                let scale = if sx < sy { sx } else { sy };
                display_width = ((display_width as f32 * scale + 0.5) as i32).max(1);
                display_height = ((display_height as f32 * scale + 0.5) as i32).max(1);
            }
        }
        (None, Some(h), true) => {
            display_height = ((h.pixels(em, viewport_height as f32) + 0.5) as i32).max(1);
            display_width =
                (display_height as f32 * (f32::from(width) / f32::from(height)) + 0.5) as i32;
            if display_height > viewport_height {
                display_height = viewport_height;
                display_width = ((display_height as f32 * (f32::from(width) / f32::from(height))
                    + 0.5) as i32)
                    .max(1);
            }
            if display_width > container_width {
                display_width = container_width;
                display_height = ((display_width as f32 * (f32::from(height) / f32::from(width))
                    + 0.5) as i32)
                    .max(1);
            }
            display_width = display_width.max(1);
        }
        (Some(w), None, true) => {
            display_width = ((w.pixels(em, container_width as f32) + 0.5) as i32)
                .min(container_width)
                .max(1);
            display_height =
                (display_width as f32 * (f32::from(height) / f32::from(width)) + 0.5) as i32;
            if display_height > viewport_height {
                display_height = viewport_height;
                display_width = ((display_height as f32 * (f32::from(width) / f32::from(height))
                    + 0.5) as i32)
                    .max(1);
            }
            display_height = display_height.max(1);
        }
        _ => {
            let sx = if i32::from(width) > container_width {
                container_width as f32 / f32::from(width)
            } else {
                1.0
            };
            let sy = if i32::from(height) > viewport_height {
                viewport_height as f32 / f32::from(height)
            } else {
                1.0
            };
            let scale = (if sx < sy { sx } else { sy }).min(1.0);
            display_width = (f32::from(width) * scale) as i32;
            display_height = (f32::from(height) * scale) as i32;
        }
    }
    (display_width, display_height)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImageResult {
    Fallback,
    Displayed,
}
impl<P: Paragraph<B>, B> Assembly<P, B> {
    /// Flush runs after probing and sizing, before the original block transition.
    pub fn process_image<S: Sink<Payload = B>>(
        &mut self,
        src: &[u8],
        css: ImageStyle,
        flow: &mut Flow,
        sink: &mut S,
        flush: impl FnOnce(&mut Self, &mut Flow, &mut S) -> Result,
    ) -> Result<ImageResult> {
        let result = sink.probe_image(src, &mut self.image_counter, &mut self.image_popup);
        let (resource, width, height) = match self.latch(result)? {
            ImageProbe::Unsupported => return Ok(ImageResult::Fallback),
            ImageProbe::Failed {
                width: _width,
                height: _height,
            } => {
                observe!(sink, "ImageReply", false, _width, _height);
                return Ok(ImageResult::Fallback);
            }
            ImageProbe::Ready {
                resource,
                width,
                height,
            } => {
                observe!(sink, "ImageReply", true, width, height);
                (resource, width, height)
            }
        };
        let em = sink.ascender(self.options.font) as f32;
        let mut container = i32::from(self.options.width);
        if let Some(paragraph) = self.paragraph.as_ref() {
            let inset = i32::from(horizontal(paragraph.style()));
            if inset > 0 && inset < container {
                container -= inset;
            }
        }
        let (width, height) =
            image_size((width, height), (container, self.options.height), em, css);
        observe!(sink, "ImageSize", width, height);
        flush(self, flow, sink)?;
        if let Some(paragraph) = self.paragraph.as_ref().filter(|p| p.size() != 0) {
            self.start_block(paragraph.style(), true, flow, sink)?;
        }
        let mut top_margin = 0i16;
        let mut bottom_margin = 0i16;
        if let Some(paragraph) = self.paragraph.as_ref().filter(|p| p.size() == 0) {
            let style = paragraph.style();
            top_margin = style.margin_top.wrapping_add(style.padding_top);
            if flow.block_depth > 1 {
                bottom_margin = flow.parent.bottom_inset();
            }
        }
        self.apply_page_break(flow, sink)?;
        if self.nonempty_page()
            && i32::from(self.next_y) + i32::from(top_margin) + height + i32::from(bottom_margin)
                > i32::from(self.options.height)
        {
            let mut carry = self.heading_carry();
            if carry > 0 {
                if let Some(page) = &self.page {
                    let carried_height = i32::from(self.next_y)
                        - i32::from(page.elements[page.elements.len() - carry].y);
                    if carried_height + i32::from(top_margin) + height
                        > i32::from(self.options.height)
                    {
                        carry = 0;
                    }
                }
            }
            self.break_carrying(carry, 0, flow.position.visible, flow, sink)?;
        } else {
            self.ensure_page();
        }
        if i32::from(self.next_y) + i32::from(top_margin) + height > i32::from(self.options.height)
        {
            top_margin =
                (i32::from(self.options.height) - height - i32::from(self.next_y)).max(0) as i16;
        }
        self.next_y = self.next_y.wrapping_add(top_margin);
        let result = sink.create_image(resource, width, height);
        let payload = self.latch(result)?;
        let x = ((i32::from(self.options.width) - width) / 2) as i16;
        self.add_element(Element {
            x,
            y: self.next_y,
            content: Content::Image(payload),
        })?;
        self.set_visible(flow.position.visible, sink);
        self.note_content(i32::from(self.next_y), i32::from(self.next_y) + height);
        self.keep_lines = 0;
        self.next_y = (i32::from(self.next_y) + height + i32::from(bottom_margin)) as i16;
        if let Some(paragraph) = self.paragraph.as_mut().filter(|p| p.size() == 0) {
            paragraph.set_style(Block {
                alignment: if self.options.alignment == 4 {
                    0
                } else {
                    self.options.alignment
                },
                ..Block::default()
            });
        }
        Ok(ImageResult::Displayed)
    }
}
