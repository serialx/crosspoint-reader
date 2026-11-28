use super::super::clippings::io::{File, Io, Mode};
use super::{alloc::vec::Vec, Error};
use core::fmt::{self, Write};

pub const PATH: &[u8] = b"/My Clippings.txt";

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Calendar {
    pub year: i32,
    pub month: i32,
    pub day: i32,
    pub weekday: i32,
    pub hour: i32,
    pub minute: i32,
}

struct Fixed<const N: usize> {
    bytes: [u8; N],
    length: usize,
}
impl<const N: usize> Fixed<N> {
    fn new() -> Self {
        Self {
            bytes: [0; N],
            length: 0,
        }
    }
    fn bytes(&self) -> &[u8] {
        &self.bytes[..self.length]
    }
}
impl<const N: usize> Write for Fixed<N> {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        let count = value.len().min(N - self.length);
        self.bytes[self.length..self.length + count].copy_from_slice(&value.as_bytes()[..count]);
        self.length += count;
        Ok(())
    }
}

fn date(clock: Option<Calendar>) -> Fixed<79> {
    const WEEKDAYS: [&str; 7] = [
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday",
        "Sunday",
    ];
    const MONTHS: [&str; 12] = [
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    ];
    let mut output = Fixed::new();
    let Some(clock) = clock.filter(|c| (100..=199).contains(&c.year)) else {
        return output;
    };
    let Some(month) = MONTHS.get(clock.month as usize) else {
        return output;
    };
    let Some(weekday) = clock
        .weekday
        .checked_add(6)
        .and_then(|w| WEEKDAYS.get((w % 7) as usize))
    else {
        return output;
    };
    let hour = match clock.hour % 12 {
        0 => 12,
        hour => hour,
    };
    let _ = write!(
        output,
        "Added on {}, {} {}, {}, {:02}:{:02} {}",
        weekday,
        month,
        clock.day as u32,
        (clock.year + 1900) as u32,
        hour,
        clock.minute,
        if clock.hour >= 12 { "PM" } else { "AM" }
    );
    output
}

#[inline(never)]
pub fn bytes(
    title: &[u8],
    author: &[u8],
    chapter: &[u8],
    page: i32,
    text: &[u8],
    clock: Option<Calendar>,
) -> Result<Vec<u8>, Error> {
    let date = date(clock);
    let mut number = Fixed::<11>::new();
    let _ = write!(number, "{page}");
    let text = &text[..xp_core::utf8::safe_truncate(&text[..text.len().min(2000)])];
    let parts: [&[u8]; 12] = [
        title,
        b" (",
        author,
        b")\n- Your Highlight on Page ",
        number.bytes(),
        if chapter.is_empty() { b"" } else { b" | " },
        chapter,
        if date.length == 0 { b"" } else { b" | " },
        date.bytes(),
        b"\n\n",
        text,
        b"\n==========\n",
    ];
    let length = parts
        .iter()
        .try_fold(0usize, |n, part| n.checked_add(part.len()))
        .ok_or(Error::Size)?;
    let mut output = Vec::new();
    output
        .try_reserve_exact(length)
        .map_err(|_| Error::Allocation)?;
    for part in parts {
        output.extend_from_slice(part);
    }
    Ok(output)
}

pub struct Clipping<'a> {
    pub title: &'a [u8],
    pub author: &'a [u8],
    pub chapter: &'a [u8],
    pub page: i32,
    pub text: &'a [u8],
}

#[inline(never)]
pub fn write<I: Io>(
    io: &I,
    clock: impl FnOnce() -> Option<Calendar>,
    clipping: Clipping<'_>,
) -> bool {
    let Some(mut file) = io.open(PATH, Mode::Append) else {
        io.log(
            1,
            format_args!("Failed to open /My Clippings.txt for append"),
            &[],
        );
        return false;
    };
    let original = file.size();
    let Ok(output) = bytes(
        clipping.title,
        clipping.author,
        clipping.chapter,
        clipping.page,
        clipping.text,
        clock(),
    ) else {
        return false;
    };
    let ok = file.write(&output) == output.len();
    let rolled_back = ok || file.truncate(original);
    file.flush();
    if !ok {
        if !rolled_back {
            io.log(
                1,
                format_args!("Failed to roll back partial write to "),
                &[PATH],
            );
        }
        io.log(1, format_args!("Failed to write clipping to "), &[PATH]);
    }
    ok
}
