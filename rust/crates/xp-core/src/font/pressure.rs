/// Only disjoint caches may be evicted; the caller excludes this entire family.
pub trait Pressure {
    fn free_default(&mut self) -> usize;
    fn render(&mut self) -> usize;
    fn sd(&mut self) -> usize;
    fn vector(&mut self) -> usize;
    /// The render sink deferred clearing this active family until its callback returned.
    fn clear_active_frame(&mut self) -> bool {
        false
    }
}
/// Same target and sink order as MemoryManager::ensureFree; the active family
/// is excluded by the pressure context, and scan/registration loans stay intact.
pub fn reclaim(pressure: &mut impl Pressure) {
    let have = pressure.free_default();
    if have >= 16 * 1024 {
        return;
    }
    evict(pressure, Some(16 * 1024 - have));
}

/// Clear every registered cache, in the same order as MemoryManager::clearCaches(0).
pub fn clear_all(pressure: &mut impl Pressure) {
    evict(pressure, None);
}

fn evict(pressure: &mut impl Pressure, target: Option<usize>) {
    let mut freed = pressure.render();
    if target.is_none_or(|target| freed < target) {
        freed += pressure.sd();
    }
    if target.is_none_or(|target| freed < target) {
        pressure.vector();
    }
}
