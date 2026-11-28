use super::{alloc::vec::Vec, Error, Record};

pub fn insert_front<N>(rows: &mut Vec<Record<N>>, row: Record<N>) -> Result<(), Error> {
    if rows.len() == rows.capacity() {
        // Match single-element vector insertion: 0 -> 1, then double when full.
        let capacity = rows.capacity().checked_mul(2).ok_or(Error::Size)?.max(1);
        rows.try_reserve_exact(capacity - rows.len())
            .map_err(|_| Error::Allocation)?;
    }
    rows.insert(0, row);
    Ok(())
}

pub fn replace_name<N>(rows: &mut [Record<N>], index: usize, name: N) -> Result<N, N> {
    match rows.get_mut(index) {
        Some(row) => Ok(core::mem::replace(&mut row.name, name)),
        None => Err(name),
    }
}

pub fn remove<N>(rows: &mut Vec<Record<N>>, index: usize) -> Option<Record<N>> {
    if index < rows.len() {
        Some(rows.remove(index))
    } else {
        None
    }
}

pub fn retain<N>(rows: &mut Vec<Record<N>>, mut keep: impl FnMut(&Record<N>) -> bool) -> usize {
    let before = rows.len();
    rows.retain(|row| keep(row));
    before - rows.len()
}
