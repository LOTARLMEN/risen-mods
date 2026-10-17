//! Spatial distance/intersection at fixed serialized poses. Not motion CCD, MTD or PhysX simulation.
//! Triangle meshes are two-sided surfaces; boxes/convexes and rounded primitives are solids.
use crate::{
    collider::{Shape, Surface},
    geometry::Mesh,
};
type P = [f64; 3];
type Bounds = [f64; 6];
const EPS: f64 = 1e-8; // centimetres; numerical zero, independent of the CLI's user tolerance
fn add(a: P, b: P) -> P {
    std::array::from_fn(|k| a[k] + b[k])
}
fn sub(a: P, b: P) -> P {
    std::array::from_fn(|k| a[k] - b[k])
}
fn mul(a: P, s: f64) -> P {
    a.map(|v| v * s)
}
fn dot(a: P, b: P) -> f64 {
    (0..3).map(|k| a[k] * b[k]).sum()
}
fn cross(a: P, b: P) -> P {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
fn pose(p: [f32; 3], m: &[f32; 16]) -> P {
    std::array::from_fn(|k| {
        m[12 + k] as f64
            + (0..3)
                .map(|r| p[r] as f64 * m[4 * r + k] as f64)
                .sum::<f64>()
    })
}
fn bounds(points: impl IntoIterator<Item = P>) -> Bounds {
    points.into_iter().fold(
        [
            f64::INFINITY,
            f64::INFINITY,
            f64::INFINITY,
            f64::NEG_INFINITY,
            f64::NEG_INFINITY,
            f64::NEG_INFINITY,
        ],
        |mut b, p| {
            for k in 0..3 {
                b[k] = b[k].min(p[k]);
                b[3 + k] = b[3 + k].max(p[k]);
            }
            b
        },
    )
}
fn bound_distance(a: Bounds, b: Bounds) -> f64 {
    (0..3)
        .map(|k| (a[k] - b[k + 3]).max(b[k] - a[k + 3]).max(0.).powi(2))
        .sum()
}

#[derive(Clone, Copy, Debug)]
struct Pair {
    d2: f64,
    a: P,
    b: P,
}
impl Pair {
    fn new(a: P, b: P) -> Self {
        Self {
            d2: dot(sub(a, b), sub(a, b)),
            a,
            b,
        }
    }
    fn same(p: P) -> Self {
        Self { d2: 0., a: p, b: p }
    }
    fn reversed(self) -> Self {
        Self {
            a: self.b,
            b: self.a,
            ..self
        }
    }
    fn keep(&mut self, p: Self) {
        if p.d2 < self.d2 {
            *self = p;
        }
    }
}
fn point_segment(p: P, a: P, b: P) -> P {
    let ab = sub(b, a);
    let d = dot(ab, ab);
    add(
        a,
        mul(
            ab,
            if d == 0. {
                0.
            } else {
                (dot(sub(p, a), ab) / d).clamp(0., 1.)
            },
        ),
    )
}
fn segments(a: P, b: P, c: P, d: P) -> Pair {
    // The constrained minimum is either the unconstrained line minimum or one of four edges
    // of the [0,1]^2 parameter domain. This also handles points and parallel segments.
    let mut best = Pair::new(a, point_segment(a, c, d));
    best.keep(Pair::new(b, point_segment(b, c, d)));
    best.keep(Pair::new(point_segment(c, a, b), c));
    best.keep(Pair::new(point_segment(d, a, b), d));
    let u = sub(b, a);
    let v = sub(d, c);
    let w = sub(a, c);
    // Cross-product determinant avoids cancellation for nearly parallel directions.
    let uv = cross(u, v);
    let det = dot(uv, uv);
    if det > 0. {
        let s = dot(cross(v, w), uv) / det;
        let t = dot(cross(u, w), uv) / det;
        if (0.0..=1.0).contains(&s) && (0.0..=1.0).contains(&t) {
            best.keep(Pair::new(add(a, mul(u, s)), add(c, mul(v, t))));
        }
    }
    best
}
fn projected_inside(p: P, t: [P; 3]) -> bool {
    let normal = cross(sub(t[1], t[0]), sub(t[2], t[0]));
    (0..3).all(|i| dot(cross(sub(t[(i + 1) % 3], t[i]), sub(p, t[i])), normal) >= 0.)
}
fn point_triangle(p: P, t: [P; 3]) -> Pair {
    let mut best = Pair::new(p, point_segment(p, t[0], t[1]));
    for i in 1..3 {
        best.keep(Pair::new(p, point_segment(p, t[i], t[(i + 1) % 3])));
    }
    let n = cross(sub(t[1], t[0]), sub(t[2], t[0]));
    let n2 = dot(n, n);
    if n2 > 0. {
        let q = sub(p, mul(n, dot(sub(p, t[0]), n) / n2));
        if projected_inside(q, t) {
            best.keep(Pair::new(p, q));
        }
    }
    best
}
fn segment_triangle(a: P, b: P, t: [P; 3]) -> Pair {
    let mut best = point_triangle(a, t);
    best.keep(point_triangle(b, t));
    for i in 0..3 {
        best.keep(segments(a, b, t[i], t[(i + 1) % 3]));
    }
    let n = cross(sub(t[1], t[0]), sub(t[2], t[0]));
    let da = dot(sub(a, t[0]), n);
    let db = dot(sub(b, t[0]), n);
    if (da <= 0. && db >= 0. || da >= 0. && db <= 0.) && da != db {
        let p = add(a, mul(sub(b, a), da / (da - db)));
        if projected_inside(p, t) {
            return Pair::same(p);
        }
    }
    best
}
fn triangles(a: [P; 3], b: [P; 3]) -> Pair {
    let mut best = segment_triangle(a[0], a[1], b);
    for i in 1..3 {
        best.keep(segment_triangle(a[i], a[(i + 1) % 3], b));
    }
    for i in 0..3 {
        best.keep(segment_triangle(b[i], b[(i + 1) % 3], a).reversed());
    }
    best
}

#[derive(Debug, Clone)]
struct Node {
    bounds: Bounds,
    start: usize,
    end: usize,
    children: Option<(usize, usize)>,
}
#[derive(Debug, Clone)]
struct Triangles {
    points: Vec<[P; 3]>,
    order: Vec<usize>,
    nodes: Vec<Node>,
    solid: bool,
}
impl Triangles {
    fn new(mesh: &Mesh, solid: bool, world: &[f32; 16]) -> Result<Self, String> {
        mesh.validate().map_err(|e| e.to_string())?;
        if mesh.triangles.is_empty() {
            return Err("contact requires nonempty collision triangles".into());
        }
        let points: Vec<[P; 3]> = mesh
            .triangles
            .iter()
            .map(|t| t.map(|i| pose(mesh.vertices[i as usize], world)))
            .collect();
        if solid {
            // Native cooked convex triangulations have slightly non-supporting tiny facets.
            // Use their closed oriented boundary, not an intersection of triangle half-spaces.
            // Weld exact duplicate positions; open/inconsistently oriented shells refuse.
            let mut vertices = std::collections::BTreeMap::new();
            let mut ids = Vec::new();
            for triangle in &points {
                for &p in triangle {
                    let next = vertices.len();
                    let key = p.map(|v| if v == 0. { 0 } else { v.to_bits() });
                    ids.push(*vertices.entry(key).or_insert(next));
                }
            }
            let mut edges = std::collections::BTreeMap::<(usize, usize), (usize, i32)>::new();
            for t in ids.chunks_exact(3) {
                for i in 0..3 {
                    let a = t[i].min(t[(i + 1) % 3]);
                    let b = t[i].max(t[(i + 1) % 3]);
                    let value = edges.entry((a, b)).or_default();
                    value.0 += 1;
                    value.1 += if t[i] < t[(i + 1) % 3] { 1 } else { -1 };
                }
            }
            if edges.values().any(|&(n, winding)| n != 2 || winding != 0) {
                return Err("solid contact mesh must be closed and consistently oriented".into());
            }
            let origin = points[0][0];
            let mut volume = 0.;
            for &t in &points {
                let n = cross(sub(t[1], t[0]), sub(t[2], t[0]));
                let length = dot(n, n).sqrt();
                if length == 0. {
                    return Err("solid contact mesh has degenerate faces".into());
                }
                volume += dot(
                    sub(t[0], origin),
                    cross(sub(t[1], origin), sub(t[2], origin)),
                );
            }
            if volume == 0. {
                return Err("solid contact mesh has no volume".into());
            }
        }
        let mut out = Self {
            order: (0..points.len()).collect(),
            points,
            nodes: Vec::new(),
            solid,
        };
        out.build(0, out.order.len());
        Ok(out)
    }
    fn build(&mut self, start: usize, end: usize) -> usize {
        let b = bounds(self.order[start..end].iter().flat_map(|&i| self.points[i]));
        let id = self.nodes.len();
        self.nodes.push(Node {
            bounds: b,
            start,
            end,
            children: None,
        });
        if end - start > 8 {
            let axis = (0..3)
                .max_by(|&a, &baxis| (b[a + 3] - b[a]).total_cmp(&(b[baxis + 3] - b[baxis])))
                .unwrap();
            let points = &self.points;
            self.order[start..end].sort_unstable_by(|&a, &b| {
                let center = |i: usize| points[i].iter().map(|p| p[axis]).sum::<f64>();
                center(a).total_cmp(&center(b))
            });
            let middle = start + (end - start) / 2;
            let left = self.build(start, middle);
            let right = self.build(middle, end);
            self.nodes[id].children = Some((left, right));
        }
        id
    }
    fn contains(&self, p: P) -> bool {
        if !self.solid {
            return false;
        }
        let b = self.nodes[0].bounds;
        if !(0..3).all(|k| p[k] >= b[k] - EPS && p[k] <= b[k + 3] + EPS) {
            return false;
        }
        // Signed solid angle, with compensated summation. The absolute winding is 4*pi inside
        // and zero outside; reversing the entire surface does not change the volume semantics.
        let mut sum = 0.;
        let mut correction = 0.;
        for t in &self.points {
            let [a, b, c] = t.map(|v| sub(v, p));
            let [la, lb, lc] = [a, b, c].map(|v| dot(v, v).sqrt());
            if la.min(lb).min(lc) <= EPS {
                return true;
            }
            let numerator = dot(a, cross(b, c));
            let denominator = la * lb * lc + dot(a, b) * lc + dot(b, c) * la + dot(c, a) * lb;
            let angle = 2. * numerator.atan2(denominator) - correction;
            let next = sum + angle;
            correction = (next - sum) - angle;
            sum = next;
        }
        sum.abs() > std::f64::consts::TAU
    }
    fn vertex_inside(&self, solid: &Self) -> Option<P> {
        if !solid.solid {
            return None;
        }
        let mut stack = vec![0];
        while let Some(i) = stack.pop() {
            let node = &self.nodes[i];
            if bound_distance(node.bounds, solid.nodes[0].bounds) > EPS * EPS {
                continue;
            }
            if let Some((a, b)) = node.children {
                stack.push(a);
                stack.push(b);
            } else {
                for &i in &self.order[node.start..node.end] {
                    for p in self.points[i] {
                        if solid.contains(p) {
                            return Some(p);
                        }
                    }
                }
            }
        }
        None
    }
    fn segment(&self, a: P, b: P) -> Pair {
        let query = bounds([a, b]);
        let mut best = Pair {
            d2: f64::INFINITY,
            a,
            b,
        };
        let mut stack = vec![0];
        while let Some(i) = stack.pop() {
            let node = &self.nodes[i];
            if bound_distance(query, node.bounds) > best.d2 {
                continue;
            }
            if let Some((l, r)) = node.children {
                let (near, far) = if bound_distance(query, self.nodes[l].bounds)
                    <= bound_distance(query, self.nodes[r].bounds)
                {
                    (l, r)
                } else {
                    (r, l)
                };
                stack.push(far);
                stack.push(near);
            } else {
                for &i in &self.order[node.start..node.end] {
                    best.keep(segment_triangle(a, b, self.points[i]));
                    if best.d2 <= EPS * EPS {
                        return best;
                    }
                }
            }
        }
        best
    }
    fn mesh(&self, other: &Self) -> Pair {
        if let Some(p) = self.vertex_inside(other) {
            return Pair::same(p);
        }
        if let Some(p) = other.vertex_inside(self) {
            return Pair::same(p);
        }
        let mut best = Pair {
            d2: f64::INFINITY,
            a: [0.; 3],
            b: [0.; 3],
        };
        let mut stack = vec![(0, 0)];
        while let Some((a, b)) = stack.pop() {
            let na = &self.nodes[a];
            let nb = &other.nodes[b];
            if bound_distance(na.bounds, nb.bounds) > best.d2 {
                continue;
            }
            let pairs = if let Some((l, r)) = na.children {
                vec![(l, b), (r, b)]
            } else if let Some((l, r)) = nb.children {
                vec![(a, l), (a, r)]
            } else {
                for &i in &self.order[na.start..na.end] {
                    for &j in &other.order[nb.start..nb.end] {
                        best.keep(triangles(self.points[i], other.points[j]));
                        if best.d2 <= EPS * EPS {
                            return best;
                        }
                    }
                }
                continue;
            };
            let mut pairs = pairs;
            pairs.sort_by(|&(a, b), &(c, d)| {
                bound_distance(self.nodes[c].bounds, other.nodes[d].bounds)
                    .total_cmp(&bound_distance(self.nodes[a].bounds, other.nodes[b].bounds))
            });
            stack.extend(pairs);
        }
        best
    }
}

#[derive(Debug, Clone)]
enum Prepared {
    Mesh(Triangles),
    Capsule { a: P, b: P, radius: f64 },
}
impl Prepared {
    fn bounds(&self) -> Bounds {
        match self {
            Self::Mesh(m) => m.nodes[0].bounds,
            Self::Capsule { a, b, radius } => {
                let mut v = bounds([*a, *b]);
                for k in 0..3 {
                    v[k] -= radius;
                    v[k + 3] += radius;
                }
                v
            }
        }
    }
    fn pair(&self, other: &Self) -> Pair {
        match (self, other) {
            (Self::Mesh(a), Self::Mesh(b)) => a.mesh(b),
            (Self::Capsule { a, b, radius }, Self::Mesh(m)) => {
                if m.contains(*a) {
                    return Pair::same(*a);
                }
                if m.contains(*b) {
                    return Pair::same(*b);
                }
                let pair = m.segment(*a, *b);
                let distance = pair.d2.sqrt();
                if distance <= *radius {
                    Pair::same(pair.b)
                } else {
                    Pair::new(
                        add(pair.a, mul(sub(pair.b, pair.a), radius / distance)),
                        pair.b,
                    )
                }
            }
            (Self::Mesh(_), Self::Capsule { .. }) => other.pair(self).reversed(),
            (
                Self::Capsule { a, b, radius: ra },
                Self::Capsule {
                    a: c,
                    b: d,
                    radius: rb,
                },
            ) => {
                let pair = segments(*a, *b, *c, *d);
                let distance = pair.d2.sqrt();
                if distance <= ra + rb {
                    if distance == 0. {
                        return Pair::same(pair.a);
                    }
                    let t = ((distance + ra - rb) * 0.5).clamp(0., distance);
                    Pair::same(add(pair.a, mul(sub(pair.b, pair.a), t / distance)))
                } else {
                    let direction = mul(sub(pair.b, pair.a), 1. / distance);
                    Pair::new(
                        add(pair.a, mul(direction, *ra)),
                        sub(pair.b, mul(direction, *rb)),
                    )
                }
            }
        }
    }
}

/// Prepared once per operation, with triangle BVHs and closed oriented native-solid boundaries.
#[derive(Debug, Clone)]
pub struct Body {
    shapes: Vec<Prepared>,
}
impl Body {
    /// Translate prepared geometry in f64 without rebuilding its BVH or re-rounding vertices.
    /// Always translate from the original body in iterative searches to avoid accumulated drift.
    pub fn translated(&self, offset: [f64; 3]) -> Result<Self, String> {
        if !offset.iter().all(|v| v.is_finite()) {
            return Err("contact translation must be finite".into());
        }
        let mut out = self.clone();
        for shape in &mut out.shapes {
            match shape {
                Prepared::Capsule { a, b, .. } => {
                    *a = add(*a, offset); *b = add(*b, offset);
                }
                Prepared::Mesh(m) => {
                    for t in &mut m.points { for p in t { *p = add(*p, offset); } }
                    for node in &mut m.nodes { for k in 0..3 {
                        node.bounds[k] += offset[k]; node.bounds[k+3] += offset[k];
                    } }
                }
            }
            if shape.bounds().iter().any(|v| !v.is_finite() || v.abs() > f32::MAX as f64) {
                return Err("translated contact geometry exceeds native coordinate range".into());
            }
        }
        Ok(out)
    }

    pub fn new(shapes: &[Shape], world: [f32; 16]) -> Result<Self, String> {
        crate::collider::scale(&world)?;
        let scale = (0..3)
            .map(|k| (world[k] as f64).powi(2))
            .sum::<f64>()
            .sqrt();
        if shapes.is_empty() {
            return Err("no enabled solid collider for contact query".into());
        }
        let shapes: Vec<_> = shapes
            .iter()
            .map(|shape| match &shape.surface {
                Surface::Mesh(m) => Ok(Prepared::Mesh(Triangles::new(m, shape.solid, &world)?)),
                Surface::Capsule { a, b, radius } => {
                    if !a.iter().chain(b).all(|v| v.is_finite())
                        || !radius.is_finite()
                        || *radius <= 0.
                    {
                        return Err("invalid curved contact collider".into());
                    }
                    Ok(Prepared::Capsule {
                        a: pose(*a, &world),
                        b: pose(*b, &world),
                        radius: *radius as f64 * scale,
                    })
                }
            })
            .collect::<Result<Vec<_>, String>>()?;
        if shapes
            .iter()
            .flat_map(|s| s.bounds())
            .any(|v| !v.is_finite() || v.abs() > f32::MAX as f64)
        {
            return Err("contact geometry exceeds native coordinate range".into());
        }
        Ok(Self { shapes })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Random(u64);
    impl Random {
        fn number(&mut self) -> f64 {
            self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1);
            ((self.0 >> 32) as u32 as f64 / u32::MAX as f64) * 40. - 20.
        }
        fn point(&mut self) -> P {
            std::array::from_fn(|_| self.number())
        }
        fn mesh(&mut self, n: usize) -> Mesh {
            Mesh {
                vertices: (0..n * 3).map(|_| self.point().map(|v| v as f32)).collect(),
                triangles: (0..n)
                    .map(|i| [3 * i as u32, 3 * i as u32 + 1, 3 * i as u32 + 2])
                    .collect(),
            }
        }
    }
    #[test]
    fn bvh_segment_and_mesh_distances_match_unpruned_triangle_search() {
        let mut rng = Random(71);
        let a = Triangles::new(&rng.mesh(53), false, &crate::lrent::IDENTITY).unwrap();
        for _ in 0..200 {
            let p = rng.point();
            let q = rng.point();
            let brute = a
                .points
                .iter()
                .map(|&t| segment_triangle(p, q, t).d2)
                .fold(f64::INFINITY, f64::min);
            assert!((a.segment(p, q).d2 - brute).abs() < 1e-10);
        }
        for _ in 0..20 {
            let mut world = crate::lrent::IDENTITY;
            world[12] = 50.;
            let b = Triangles::new(&rng.mesh(17), false, &world).unwrap();
            let brute = a
                .points
                .iter()
                .flat_map(|&a| b.points.iter().map(move |&b| triangles(a, b).d2))
                .fold(f64::INFINITY, f64::min);
            assert!((a.mesh(&b).d2 - brute).abs() < 1e-10);
        }
    }
    #[test]
    fn sphere_box_queries_match_independent_coordinate_clamping_distance() {
        let mut rng = Random(99);
        let mesh =
            crate::collider::box_mesh([0.; 3], [5., 7., 9.], [1., 0., 0., 0., 1., 0., 0., 0., 1.])
                .unwrap();
        let box_body = Body::new(
            &[Shape {
                surface: Surface::Mesh(mesh),
                solid: true,
            }],
            crate::lrent::IDENTITY,
        )
        .unwrap();
        for _ in 0..500 {
            let p = rng.point().map(|v| v as f32);
            let radius = (rng.number().abs() * 0.1 + 0.01) as f32;
            let ball = Body::new(
                &[Shape {
                    surface: Surface::Capsule { a: p, b: p, radius },
                    solid: true,
                }],
                crate::lrent::IDENTITY,
            )
            .unwrap();
            let expected = ((0..3)
                .map(|k| (p[k].abs() as f64 - [5., 7., 9.][k]).max(0.).powi(2))
                .sum::<f64>()
                .sqrt()
                - radius as f64)
                .max(0.);
            let actual = query(&box_body, &ball);
            assert!(
                (actual.distance_cm - expected).abs() < 1e-8,
                "{p:?}: {actual:?}, expected {expected}"
            );
            assert_eq!(actual.intersects, expected <= EPS);
        }
    }
}

#[derive(Debug, Clone)]
pub struct Contact {
    pub distance_cm: f64,
    /// Includes boundary touching, with a 1e-8 cm numerical-zero threshold.
    pub intersects: bool,
    /// Closest points when separated, a shared point when intersecting (may be inside a solid).
    pub point_a: P,
    pub point_b: P,
    pub shape_a: usize,
    pub shape_b: usize,
}
/// Distance/contact with an infinite horizontal, two-sided plane. No finite proxy or sampling.
/// The plane is target shape 0; touching and crossing share the same zero-distance result.
pub fn query_horizontal_plane(a: &Body, height: f64) -> Result<Contact, String> {
    if !height.is_finite() || height.abs() > f32::MAX as f64 {
        return Err("contact plane height must be finite and within native coordinate range".into());
    }
    let mut best = Pair { d2: f64::INFINITY, a: [0.; 3], b: [0.; 3] };
    let mut shape_a = 0;
    let projection = |p: P| Pair::new(p, [p[0], height, p[2]]);
    for (i, shape) in a.shapes.iter().enumerate() {
        let pair = match shape {
            Prepared::Capsule { a, b, radius } => {
                let low = if a[1] < b[1] { *a } else { *b };
                let high = if a[1] > b[1] { *a } else { *b };
                if low[1] - radius > height {
                    projection(sub(low, [0., *radius, 0.]))
                } else if high[1] + radius < height {
                    projection(add(high, [0., *radius, 0.]))
                } else {
                    let t = if high[1] > low[1] {
                        ((height - low[1]) / (high[1] - low[1])).clamp(0., 1.)
                    } else { 0. };
                    let p = add(low, mul(sub(high, low), t));
                    Pair::same([p[0], height, p[2]])
                }
            }
            Prepared::Mesh(m) => {
                let mut best = Pair { d2: f64::INFINITY, a: [0.; 3], b: [0.; 3] };
                for triangle in &m.points {
                    for k in 0..3 {
                        let p = triangle[k];
                        let q = triangle[(k + 1) % 3];
                        let candidate = if (p[1] < height && q[1] > height)
                            || (q[1] < height && p[1] > height) {
                            let hit = add(p, mul(sub(q, p), (height - p[1]) / (q[1] - p[1])));
                            Pair::same([hit[0], height, hit[2]])
                        } else { projection(p) };
                        if candidate.d2 < best.d2 { best = candidate; }
                    }
                    if best.d2 == 0. { break; }
                }
                best
            }
        };
        if pair.d2 < best.d2 { best = pair; shape_a = i; }
        if best.d2 == 0. { break; }
    }
    let intersects = best.d2 <= EPS * EPS;
    if intersects { best = Pair::same(mul(add(best.a, best.b), 0.5)); }
    Ok(Contact { distance_cm: best.d2.sqrt(), intersects,
        point_a: best.a, point_b: best.b, shape_a, shape_b: 0 })
}

pub fn query(a: &Body, b: &Body) -> Contact {
    let mut best = Pair {
        d2: f64::INFINITY,
        a: [0.; 3],
        b: [0.; 3],
    };
    let mut shapes = (0, 0);
    'pairs: for (i, a) in a.shapes.iter().enumerate() {
        for (j, b) in b.shapes.iter().enumerate() {
            if bound_distance(a.bounds(), b.bounds()) > best.d2 {
                continue;
            }
            let pair = a.pair(b);
            if pair.d2 < best.d2 {
                best = pair;
                shapes = (i, j);
            }
            if best.d2 <= EPS * EPS {
                break 'pairs;
            }
        }
    }
    let intersects = best.d2 <= EPS * EPS;
    if intersects {
        best = Pair::same(mul(add(best.a, best.b), 0.5));
    }
    Contact {
        distance_cm: best.d2.sqrt(),
        intersects,
        point_a: best.a,
        point_b: best.b,
        shape_a: shapes.0,
        shape_b: shapes.1,
    }
}
