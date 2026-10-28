//! Commands are validated before any pixel is written. Text uses the existing
//! font service; geometry uses R1's orientation-aware packed target.
use crate::scene::{Command, Error, Kind, Rect, Scene, MAX_SCOPES};
use embedded_graphics_core::{
    draw_target::DrawTarget,
    geometry::{Point, Size},
    pixelcolor::BinaryColor,
    primitives::Rectangle,
    Pixel,
};
use xp_core::strip::Strip;

#[derive(Clone, Copy, Default)]
struct Scope {
    clip: Rect,
    x: i32,
    y: i32,
}

pub trait Sink {
    fn resource_valid(&mut self, handle: u32, generation: u32) -> bool;
    fn paint(&mut self, command: &Command, clip: Rect, text: &[u8]);
}

fn intersection(a: Rect, b: Rect) -> Rect {
    let x = i32::from(a.x).max(i32::from(b.x));
    let y = i32::from(a.y).max(i32::from(b.y));
    let right = (i32::from(a.x) + i32::from(a.w)).min(i32::from(b.x) + i32::from(b.w));
    let bottom = (i32::from(a.y) + i32::from(a.h)).min(i32::from(b.y) + i32::from(b.h));
    Rect {
        x: x as i16,
        y: y as i16,
        w: (right - x).max(0) as i16,
        h: (bottom - y).max(0) as i16,
    }
}
fn translated(rect: Rect, scope: Scope) -> Result<Rect, Error> {
    Ok(Rect {
        x: i16::try_from(i32::from(rect.x) + scope.x).map_err(|_| Error::Invalid)?,
        y: i16::try_from(i32::from(rect.y) + scope.y).map_err(|_| Error::Invalid)?,
        ..rect
    })
}

/// The caller serializes scene and resource lifetimes across validation/paint.
pub fn render(scene: &Scene<'_>, screen: Rect, sink: &mut dyn Sink) -> Result<(), Error> {
    render_layers(&[scene], screen, sink)
}

/// Frozen parents and overlays are all checked before the first pixel changes.
pub fn render_layers(
    scenes: &[&Scene<'_>],
    screen: Rect,
    sink: &mut dyn Sink,
) -> Result<(), Error> {
    if scenes.len() > 4 {
        return Err(Error::Capacity);
    }
    for scene in scenes {
        validate(scene, screen, sink)?;
    }
    for scene in scenes {
        walk(scene, screen, Some(sink))?;
    }
    Ok(())
}
pub fn validate(scene: &Scene<'_>, screen: Rect, sink: &mut dyn Sink) -> Result<(), Error> {
    scene.validate(|h, g| sink.resource_valid(h, g))?;
    walk(scene, screen, None)
}
fn walk(scene: &Scene<'_>, screen: Rect, mut sink: Option<&mut dyn Sink>) -> Result<(), Error> {
    let mut scopes = [Scope::default(); MAX_SCOPES];
    let mut current = Scope {
        clip: screen,
        x: 0,
        y: 0,
    };
    let mut depth: usize = 0;
    for command in scene.commands {
        let kind = Kind::from_byte(command.kind).ok_or(Error::Invalid)?;
        match kind {
            Kind::Clip | Kind::Translate => {
                let slot = scopes.get_mut(depth).ok_or(Error::Capacity)?;
                *slot = current;
                depth += 1;
                if kind == Kind::Clip {
                    current.clip = intersection(current.clip, translated(command.rect, current)?);
                } else {
                    current.x += i32::from(command.rect.x);
                    current.y += i32::from(command.rect.y);
                }
            }
            Kind::Pop => {
                depth = depth.checked_sub(1).ok_or(Error::Invalid)?;
                current = *scopes.get(depth).ok_or(Error::Invalid)?;
            }
            _ => {
                let command = Command {
                    rect: translated(command.rect, current)?,
                    ..*command
                };
                if let Some(sink) = sink.as_deref_mut() {
                    let text = if kind == Kind::Text {
                        let start = usize::from(command.text_start);
                        &scene.text[start..start + usize::from(command.text_len) + 1]
                    } else {
                        &[]
                    };
                    sink.paint(&command, current.clip, text);
                }
            }
        }
    }
    Ok(())
}

fn fill(target: &mut Strip<'_>, rect: Rect, paint: u8) {
    if rect.w <= 0 || rect.h <= 0 {
        return;
    }
    let area = Rectangle::new(
        Point::new(i32::from(rect.x), i32::from(rect.y)),
        Size::new(rect.w as u32, rect.h as u32),
    );
    if paint < 2 {
        let _ = target.fill_solid(
            &area,
            if paint == 0 {
                BinaryColor::Off
            } else {
                BinaryColor::On
            },
        );
    } else if paint == 2 {
        target.surface().shade(
            i32::from(rect.x),
            i32::from(rect.y),
            i32::from(rect.w),
            i32::from(rect.h),
        );
    } else {
        let pixels = (i32::from(rect.y)..i32::from(rect.y) + i32::from(rect.h)).flat_map(|y| {
            (i32::from(rect.x)..i32::from(rect.x) + i32::from(rect.w)).map(move |x| {
                let ink = (x + y) & 1 == 0;
                Pixel(
                    Point::new(x, y),
                    if ink {
                        BinaryColor::Off
                    } else {
                        BinaryColor::On
                    },
                )
            })
        });
        let _ = target.draw_iter(pixels);
    }
}
fn band(target: &mut Strip<'_>, x: i32, y: i32, w: i32, h: i32, paint: u8) {
    let (Ok(x), Ok(y), Ok(w), Ok(h)) = (
        i16::try_from(x),
        i16::try_from(y),
        i16::try_from(w),
        i16::try_from(h),
    ) else {
        return;
    };
    fill(target, Rect { x, y, w, h }, paint);
}
fn rounded(target: &mut Strip<'_>, rect: Rect, radius: u8, paint: u8, corners: u8) {
    let (x, y, w, h) = (
        i32::from(rect.x),
        i32::from(rect.y),
        i32::from(rect.w),
        i32::from(rect.h),
    );
    let mask = if corners == 0 { 15 } else { corners };
    let sides = if mask == 15 { 2 } else { 1 };
    let r = i32::from(radius).min(w / sides).min(h / sides);
    if r <= 0 {
        fill(target, rect, paint);
        return;
    }
    band(target, x + r + 1, y, w - 2 * r - 2, h, paint);
    for (left, top_bit, bottom_bit) in [(x, 1, 4), (x + w - r - 1, 2, 8)] {
        let top = y + if mask & top_bit != 0 { r + 1 } else { 0 };
        let bottom = y + h - 1 - if mask & bottom_bit != 0 { r + 1 } else { 0 };
        band(target, left, top, r + 1, bottom - top + 1, paint);
    }
    for (bit, cx, cy, dx, dy) in [
        (1, x + r, y + r, -1, -1),
        (2, x + w - r - 1, y + r, 1, -1),
        (8, x + w - r - 1, y + h - r - 1, 1, 1),
        (4, x + r, y + h - r - 1, -1, 1),
    ] {
        if mask & bit == 0 {
            continue;
        }
        let mut extent = r;
        for row in 0..=r {
            while extent > 0 && extent * extent + row * row > r * r {
                extent -= 1;
            }
            band(
                target,
                if dx < 0 { cx - extent } else { cx },
                cy + dy * row,
                extent + 1,
                1,
                paint,
            );
        }
    }
}

fn stroke(target: &mut Strip<'_>, command: &Command) {
    let rect = command.rect;
    let (x, y, w, h) = (
        i32::from(rect.x),
        i32::from(rect.y),
        i32::from(rect.w),
        i32::from(rect.h),
    );
    if w <= 0 || h <= 0 || command.width == 0 {
        return;
    }
    let radius = i32::from(command.radius).min(w / 2).min(h / 2);
    if radius <= 0 {
        for inset in 0..i32::from(command.width) {
            let (left, right, top, bottom) =
                (x + inset, x + w - 1 - inset, y + inset, y + h - 1 - inset);
            band(
                target,
                left.min(right),
                top,
                (right - left).abs() + 1,
                1,
                command.paint,
            );
            band(
                target,
                left.min(right),
                bottom,
                (right - left).abs() + 1,
                1,
                command.paint,
            );
            band(
                target,
                left,
                top.min(bottom),
                1,
                (bottom - top).abs() + 1,
                command.paint,
            );
            band(
                target,
                right,
                top.min(bottom),
                1,
                (bottom - top).abs() + 1,
                command.paint,
            );
        }
        return;
    }
    let width = i32::from(command.width).min(radius);
    let inner = radius - width;
    let mask = if command.corners == 0 {
        15
    } else {
        command.corners
    };
    if mask & 3 != 0 {
        band(target, x + radius, y, w - 2 * radius, width, command.paint);
    }
    if mask & 12 != 0 {
        band(
            target,
            x + radius,
            y + h - width,
            w - 2 * radius,
            width,
            command.paint,
        );
    }
    if mask & 5 != 0 {
        band(target, x, y + radius, width, h - 2 * radius, command.paint);
    }
    if mask & 10 != 0 {
        band(
            target,
            x + w - width,
            y + radius,
            width,
            h - 2 * radius,
            command.paint,
        );
    }
    for (bit, cx, cy, dx, dy) in [
        (1, x + radius, y + radius, -1, -1),
        (2, x + w - radius - 1, y + radius, 1, -1),
        (8, x + w - radius - 1, y + h - radius - 1, 1, 1),
        (4, x + radius, y + h - radius - 1, -1, 1),
    ] {
        if mask & bit == 0 {
            continue;
        }
        let mut outer_x = radius;
        let mut inner_x = inner;
        for row in 0..=radius {
            while outer_x > 0 && outer_x * outer_x + row * row > radius * radius {
                outer_x -= 1;
            }
            while inner_x > 0 && (inner_x - 1) * (inner_x - 1) + row * row >= inner * inner {
                inner_x -= 1;
            }
            if outer_x >= inner_x {
                let left = cx + dx * inner_x;
                let right = cx + dx * outer_x;
                band(
                    target,
                    left.min(right),
                    cy + dy * row,
                    (right - left).abs() + 1,
                    1,
                    command.paint,
                );
            }
        }
    }
}

#[inline(never)]
pub fn geometry(target: &mut Strip<'_>, command: &Command) {
    match Kind::from_byte(command.kind) {
        Some(Kind::Clear) => target.clear_plane(),
        Some(Kind::Triangle) => {
            let r = command.rect;
            let h = i32::from(r.h);
            let w = i32::from(r.w);
            let half = h / 2;
            if half > 0 && h - half > 0 {
                for dy in 1..=h {
                    let extent = if dy <= half {
                        dy * w / half
                    } else {
                        w + (dy - half) * (-w) / (h - half)
                    };
                    band(
                        target,
                        i32::from(r.x),
                        i32::from(r.y) + dy,
                        extent + 1,
                        1,
                        command.paint,
                    );
                }
            }
        }
        Some(Kind::Fill) => rounded(
            target,
            command.rect,
            command.radius,
            command.paint,
            command.corners,
        ),
        Some(Kind::Stroke) => stroke(target, command),
        Some(Kind::Line) => {
            let mut x = i32::from(command.rect.x);
            let mut y = i32::from(command.rect.y);
            let x1 = x + i32::from(command.rect.w);
            let y1 = y + i32::from(command.rect.h);
            let dx = (x1 - x).abs();
            let dy = -(y1 - y).abs();
            let sx = if x < x1 { 1 } else { -1 };
            let sy = if y < y1 { 1 } else { -1 };
            let mut error = dx + dy;
            loop {
                band(target, x, y, 1, 1, command.paint);
                if x == x1 && y == y1 {
                    break;
                }
                let twice = 2 * error;
                if twice > dy {
                    error += dy;
                    x += sx;
                }
                if twice < dx {
                    error += dx;
                    y += sy;
                }
            }
        }
        _ => {}
    }
}
