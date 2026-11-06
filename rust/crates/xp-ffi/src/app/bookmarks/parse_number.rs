// ArduinoJson 7.4.2 parseNumber/FloatTraits algorithm, adapted to Rust.
// Copyright (c) 2014-2025, Benoit BLANCHON. See ARDUINOJSON-LICENSE.txt.
use super::Error;

pub enum Number {
    Unsigned(u64),
    Signed(i64),
    Float(f32),
    Double(f64),
}
impl Number {
    pub fn percentage(&self) -> f32 {
        match *self {
            Self::Unsigned(n) => n as f32,
            Self::Signed(n) => n as f32,
            Self::Float(n) => n,
            Self::Double(n) => n as f32,
        }
    }
    pub fn unsigned(&self, maximum: u32) -> u32 {
        let n = match *self {
            Self::Unsigned(n) => n,
            Self::Signed(n) if n >= 0 => n as u64,
            _ => return 0,
        };
        if n <= u64::from(maximum) {
            n as u32
        } else {
            0
        }
    }
}

pub fn parse(bytes: &[u8]) -> Result<Number, Error> {
    let mut index = 0;
    let at = |i| bytes.get(i).copied().unwrap_or(0);
    let negative = at(0) == b'-';
    if matches!(at(0), b'-' | b'+') {
        index += 1;
    }
    if !at(index).is_ascii_digit() && at(index) != b'.' {
        return Err(Error::InvalidInput);
    }
    let mut mantissa = 0u64;
    let mut offset = 0i16;
    while at(index).is_ascii_digit() {
        let digit = u64::from(at(index) - b'0');
        if mantissa > u64::MAX / 10 {
            break;
        }
        mantissa *= 10;
        if mantissa > u64::MAX - digit {
            break;
        }
        mantissa += digit;
        index += 1;
    }
    if index == bytes.len() {
        if !negative {
            return Ok(Number::Unsigned(mantissa));
        }
        if mantissa <= 1 << 63 {
            return Ok(Number::Signed((mantissa as i64).wrapping_neg()));
        }
    }
    const MAX_MANTISSA: u64 = (1 << 52) - 1;
    while mantissa > MAX_MANTISSA {
        mantissa /= 10;
        offset += 1;
    }
    while at(index).is_ascii_digit() {
        offset += 1;
        index += 1;
    }
    if at(index) == b'.' {
        index += 1;
        while at(index).is_ascii_digit() {
            if mantissa < MAX_MANTISSA / 10 {
                mantissa = mantissa * 10 + u64::from(at(index) - b'0');
                offset -= 1;
            }
            index += 1;
        }
    }
    let mut exponent = 0i16;
    if matches!(at(index), b'e' | b'E') {
        index += 1;
        let negative_exponent = at(index) == b'-';
        if matches!(at(index), b'-' | b'+') {
            index += 1;
        }
        while at(index).is_ascii_digit() {
            exponent = exponent * 10 + i16::from(at(index) - b'0');
            if exponent + offset > 308 {
                let n = if negative_exponent {
                    0.0
                } else {
                    f64::INFINITY
                };
                return Ok(Number::Double(if negative { -n } else { n }));
            }
            index += 1;
        }
        if negative_exponent {
            exponent = -exponent;
        }
    }
    exponent += offset;
    if index != bytes.len() {
        return Err(Error::InvalidInput);
    }
    if !(-38..=38).contains(&exponent) || mantissa > (1 << 23) - 1 {
        const POSITIVE: [f64; 9] = [1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256];
        const NEGATIVE: [f64; 9] = [1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32, 1e-64, 1e-128, 1e-256];
        let powers = if exponent > 0 { &POSITIVE } else { &NEGATIVE };
        let mut value = mantissa as f64;
        let mut power = exponent.unsigned_abs();
        for factor in powers {
            if power & 1 != 0 {
                value *= factor;
            }
            power >>= 1;
        }
        Ok(Number::Double(if negative { -value } else { value }))
    } else {
        const POSITIVE: [f32; 6] = [1e1, 1e2, 1e4, 1e8, 1e16, 1e32];
        const NEGATIVE: [f32; 6] = [1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32];
        let powers = if exponent > 0 { &POSITIVE } else { &NEGATIVE };
        let mut value = mantissa as f32;
        let mut power = exponent.unsigned_abs();
        for factor in powers {
            if power & 1 != 0 {
                value *= factor;
            }
            power >>= 1;
        }
        Ok(Number::Float(if negative { -value } else { value }))
    }
}
