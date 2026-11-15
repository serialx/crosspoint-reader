// ArduinoJson 7.4.2 FloatParts/FloatTraits/TextFormatter algorithm, adapted to Rust.
// Copyright (c) 2014-2025, Benoit BLANCHON. See ARDUINOJSON-LICENSE.txt.

pub fn integer(mut value: u32, buffer: &mut [u8; 32]) -> &[u8] {
    let mut start = buffer.len();
    loop {
        start -= 1;
        buffer[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            return &buffer[start..];
        }
    }
}

// The field is stored as float32 by ArduinoJson, then normalized as JsonFloat
// (double) with six decimal places. Rust's shortest formatter differs on disk.
pub fn float(value: f32, buffer: &mut [u8; 32]) -> &[u8] {
    if !value.is_finite() {
        return b"null";
    }
    let mut used = 0;
    let mut value = f64::from(value);
    if value < 0.0 {
        buffer[used] = b'-';
        used += 1;
        value = -value;
    }
    let mut exponent = 0i16;
    // A finite float32 needs only these six powers, even in double arithmetic.
    const POSITIVE: [f64; 6] = [1e1, 1e2, 1e4, 1e8, 1e16, 1e32];
    const NEGATIVE: [f64; 6] = [1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32];
    if value >= 1e7 {
        for index in (0..6).rev() {
            if value >= POSITIVE[index] {
                value *= NEGATIVE[index];
                exponent += 1 << index;
            }
        }
    }
    if value > 0.0 && value <= 1e-5 {
        for index in (0..6).rev() {
            if value < NEGATIVE[index] * 10.0 {
                value *= POSITIVE[index];
                exponent -= 1 << index;
            }
        }
    }
    let mut integral = value as u32;
    let mut maximum = 1_000_000u32;
    let mut places = 6;
    let mut remaining = integral;
    while remaining >= 10 {
        maximum /= 10;
        places -= 1;
        remaining /= 10;
    }
    let remainder = (value - f64::from(integral)) * f64::from(maximum);
    let mut decimal = remainder as u32;
    decimal += ((remainder - f64::from(decimal)) * 2.0) as u32;
    if decimal >= maximum {
        decimal = 0;
        integral += 1;
        if exponent != 0 && integral >= 10 {
            exponent += 1;
            integral = 1;
        }
    }
    while decimal.is_multiple_of(10) && places > 0 {
        decimal /= 10;
        places -= 1;
    }
    append_integer(buffer, &mut used, integral);
    if places > 0 {
        buffer[used] = b'.';
        used += 1;
        let end = used + places;
        for slot in (used..end).rev() {
            buffer[slot] = b'0' + (decimal % 10) as u8;
            decimal /= 10;
        }
        used = end;
    }
    if exponent != 0 {
        buffer[used] = b'e';
        used += 1;
        if exponent < 0 {
            buffer[used] = b'-';
            used += 1;
            exponent = -exponent;
        }
        append_integer(buffer, &mut used, exponent as u32);
    }
    &buffer[..used]
}

fn append_integer(buffer: &mut [u8; 32], used: &mut usize, mut value: u32) {
    let start = *used;
    loop {
        buffer[*used] = b'0' + (value % 10) as u8;
        *used += 1;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    buffer[start..*used].reverse();
}
