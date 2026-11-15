use core::fmt;

// Native diagnostics promote f32 to double and use fixed, ties-to-even decimals.
pub(super) struct Decimal<const DIGITS: u32>(pub(super) f32);
#[inline(never)]
fn large(out: &mut fmt::Formatter<'_>, significand: u32, shift: u32, digits: u32) -> fmt::Result {
    let mut limbs = [significand, 0, 0, 0, 0];
    for _ in 0..shift {
        let mut carry = 0;
        for limb in &mut limbs {
            let doubled = *limb * 2 + carry;
            carry = u32::from(doubled >= 1_000_000_000);
            *limb = doubled - carry * 1_000_000_000;
        }
    }
    let mut values = limbs.iter().rev().skip_while(|&&limb| limb == 0);
    write!(out, "{}", values.next().expect("nonzero significand"))?;
    for limb in values {
        write!(out, "{limb:09}")?;
    }
    out.write_str(".")?;
    for _ in 0..digits {
        out.write_str("0")?;
    }
    Ok(())
}
impl<const DIGITS: u32> fmt::Display for Decimal<DIGITS> {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        assert!((1..=3).contains(&DIGITS));
        let bits = self.0.to_bits();
        if bits >> 31 != 0 {
            out.write_str("-")?;
        }
        let magnitude = f32::from_bits(bits & 0x7fff_ffff);
        if magnitude.is_nan() {
            return out.write_str("nan");
        }
        if magnitude.is_infinite() {
            return out.write_str("inf");
        }
        if magnitude >= 16_777_216.0 {
            return large(
                out,
                (bits & 0x7f_ffff) | 0x80_0000,
                ((bits >> 23) & 0xff) - 150,
                DIGITS,
            );
        }
        let factor = 10u64.pow(DIGITS);
        let scaled = f64::from(magnitude) * factor as f64;
        let floor = scaled as u64;
        let fraction = scaled - floor as f64;
        let rounded =
            floor + u64::from(fraction > 0.5 || (fraction == 0.5 && !floor.is_multiple_of(2)));
        write!(
            out,
            "{}.{:0width$}",
            rounded / factor,
            rounded % factor,
            width = DIGITS as usize
        )
    }
}
