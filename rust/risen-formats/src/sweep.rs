//! Conservative, fixed-orientation downward translation using full-geometry distances.
//! This is not rigid-body simulation, an MTD solver, or an arbitrary initial-overlap sweep.
use crate::contact::{Body, Contact, query, query_horizontal_plane};

#[derive(Clone, Copy)]
pub enum Target<'a> {
    Body(&'a Body),
    HorizontalPlane(f64),
}

pub const MAX_ITERATIONS: usize = 512;
pub const MIN_TOLERANCE_CM: f64 = 1e-6;
pub const MIN_CLOSING_RATIO: f64 = 1e-4;

#[derive(Debug)]
pub struct DownwardSweep {
    /// Translation from the supplied moving pose, on the clear side of first contact.
    pub offset_y_cm: f64,
    pub iterations: usize,
    pub target: usize,
    pub contact: Contact,
}

pub fn nearest(moving: &Body, targets: &[Target<'_>]) -> Result<(usize, Contact), String> {
    let mut best: Option<(usize, Contact)> = None;
    for (i, target) in targets.iter().enumerate() {
        let q = match target {
            Target::Body(body) => query(moving, body),
            Target::HorizontalPlane(y) => query_horizontal_plane(moving, *y)?,
        };
        if !q.distance_cm.is_finite() || q.distance_cm < 0. {
            return Err("nonfinite or negative sweep distance".into());
        }
        if best.as_ref().is_none_or(|(_, b)| q.distance_cm < b.distance_cm) {
            best = Some((i, q));
        }
    }
    best.ok_or_else(|| "vertical sweep requires at least one complete target".into())
}

/// Search from start down to end (both offsets from `moving`). Each step is 90% of the current
/// nearest spatial distance: translation by less than that distance cannot reach any target.
/// Stop on the clear side within spatial tolerance, with a closest-point direction facing down.
/// A close parallel wall, initial touching/overlap, numerical failure or iteration limit refuses.
/// Disconnected surfaces/overhangs select the first obstacle reached from the top, never a pocket
/// reached by binary searching a nonmonotone overlap predicate. Tolerance is NOT vertical error.
pub fn downward(moving: &Body, targets: &[Target<'_>], start: f64, end: f64, tolerance: f64)
    -> Result<DownwardSweep, String>
{
    if ![start, end, tolerance].iter().all(|v| v.is_finite()) || end > start
        || tolerance < MIN_TOLERANCE_CM {
        return Err("invalid vertical sweep range/tolerance (minimum tolerance 1e-6 cm)".into());
    }
    let mut offset = start;
    for iteration in 1..=MAX_ITERATIONS {
        let pose = moving.translated([0., offset, 0.])?;
        let (target, contact) = nearest(&pose, targets)?;
        if contact.intersects {
            return Err(if iteration == 1 {
                "vertical sweep starts touching/overlapping; increase --max-rise or select different supports"
            } else {
                "vertical sweep encountered numerical intersection; refusing unsafe placement"
            }.into());
        }
        if contact.distance_cm <= tolerance {
            let closing = (contact.point_a[1] - contact.point_b[1]) / contact.distance_cm;
            if closing <= MIN_CLOSING_RATIO {
                return Err("vertical sweep approaches a side/grazing surface, not a supported downward contact".into());
            }
            return Ok(DownwardSweep { offset_y_cm: offset, iterations: iteration, target, contact });
        }
        if offset <= end {
            return Err("no downward contact within vertical sweep limits".into());
        }
        let next = (offset - 0.9 * contact.distance_cm).max(end);
        if next >= offset {
            return Err("vertical sweep cannot make numerical progress".into());
        }
        offset = next;
    }
    Err("vertical sweep did not converge within 512 queries; side/grazing geometry may require a different placement".into())
}
