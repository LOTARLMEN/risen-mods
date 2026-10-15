//! Offline, serialized solid collision surfaces. No dynamic simulation or collision-group matrix.
use crate::{
    collision::{self, Primitive},
    geometry::Mesh,
    lrent::{EMPTY_BOX, IDENTITY, box_union, transform_vector},
    props::PropertyObject,
    xcom::{CollisionMesh, Kind},
};
use std::sync::Arc;

/// Analytic curved supports avoid replacing spheres/capsules by faceted approximations.
#[derive(Debug, Clone)]
pub enum Surface {
    Mesh(Mesh),
    Capsule {
        a: [f32; 3],
        b: [f32; 3],
        radius: f32,
    }, // a == b is a sphere
}
pub trait VerticalSurface {
    fn surface_y(&self, x: f32, z: f32, bottom: f32, top: f32) -> Option<f32>;
}
impl VerticalSurface for Mesh {
    fn surface_y(&self, x: f32, z: f32, bottom: f32, top: f32) -> Option<f32> {
        Mesh::surface_y(self, x, z, bottom, top)
    }
}
impl VerticalSurface for Surface {
    fn surface_y(&self, x: f32, z: f32, bottom: f32, top: f32) -> Option<f32> {
        match self {
            Self::Mesh(m) => m.surface_y(x, z, bottom, top),
            Self::Capsule { a, b, radius } => {
                // Infinite vertical line against a finite cylinder plus its spherical caps.
                // Use centered f64 arithmetic to retain precision at distant world coordinates.
                let a = a.map(f64::from);
                let b = b.map(f64::from);
                let radius = *radius as f64;
                let p = [x as f64, 0., z as f64];
                let axis = std::array::from_fn::<_, 3, _>(|k| b[k] - a[k]);
                let length = dot(axis, axis).sqrt();
                let mut hits = Vec::new();
                let mut sphere = |c: [f64; 3], cap: i32| {
                    let rad = radius * radius - (p[0] - c[0]).powi(2) - (p[2] - c[2]).powi(2);
                    if rad >= -1e-8 * radius * radius {
                        for y in [c[1] - rad.max(0.).sqrt(), c[1] + rad.max(0.).sqrt()] {
                            let along = if length > 1e-12 {
                                dot([p[0] - a[0], y - a[1], p[2] - a[2]], axis) / length
                            } else {
                                0.
                            };
                            if cap == 0
                                || cap < 0 && along <= 1e-7
                                || cap > 0 && along >= length - 1e-7
                            {
                                hits.push(y);
                            }
                        }
                    }
                };
                if length < 1e-12 {
                    sphere(a, 0);
                } else {
                    sphere(a, -1);
                    sphere(b, 1);
                }
                if length > 1e-12 {
                    let u = axis.map(|v| v / length);
                    let q = std::array::from_fn(|k| p[k] - a[k]);
                    let qu = dot(q, u);
                    let aa = 1. - u[1] * u[1];
                    let bb = 2. * (q[1] - qu * u[1]);
                    let cc = dot(q, q) - qu * qu - radius * radius;
                    let disc = bb * bb - 4. * aa * cc;
                    if aa > 1e-12 && disc >= 0. {
                        for y in [
                            (-bb - disc.sqrt()) / (2. * aa),
                            (-bb + disc.sqrt()) / (2. * aa),
                        ] {
                            let along = qu + y * u[1];
                            if along >= -1e-7 && along <= length + 1e-7 {
                                hits.push(y);
                            }
                        }
                    }
                }
                hits.into_iter()
                    .filter(|&y| y.is_finite() && y >= bottom as f64 && y <= top as f64)
                    .max_by(f64::total_cmp)
                    .map(|y| y as f32)
            }
        }
    }
}
fn dot(a: [f64; 3], b: [f64; 3]) -> f64 {
    (0..3).map(|k| a[k] * b[k]).sum()
}
fn point(p: [f32; 3], m: &[f32; 16]) -> [f32; 3] {
    let v = transform_vector(p, m);
    std::array::from_fn(|k| v[k] + m[12 + k])
}
impl Surface {
    pub fn bounds(&self) -> Option<[f32; 6]> {
        match self {
            Self::Mesh(m) => m.bounds(),
            Self::Capsule { a, b, radius } => Some([
                a[0].min(b[0]) - radius,
                a[1].min(b[1]) - radius,
                a[2].min(b[2]) - radius,
                a[0].max(b[0]) + radius,
                a[1].max(b[1]) + radius,
                a[2].max(b[2]) + radius,
            ]),
        }
    }
}

/// Positive uniform scale and a rigid orientation, as supported by the native creation path.
/// Triangle meshes cannot be scaled by Rev.2045 (Engine 0x65774d); refuse instead of inventing one.
fn scale(m: &[f32; 16]) -> Result<f32, String> {
    if !m.iter().all(|v| v.is_finite())
        || [m[3], m[7], m[11]] != [0.; 3]
        || (m[15] - 1.).abs() > 1e-3
    {
        return Err("invalid collision pose".into());
    }
    let rows: [[f64; 3]; 3] = std::array::from_fn(|r| std::array::from_fn(|k| m[r * 4 + k] as f64));
    let lengths = rows.map(|r| dot(r, r).sqrt());
    let s = lengths[0];
    let det = rows[0][0] * (rows[1][1] * rows[2][2] - rows[1][2] * rows[2][1])
        - rows[0][1] * (rows[1][0] * rows[2][2] - rows[1][2] * rows[2][0])
        + rows[0][2] * (rows[1][0] * rows[2][1] - rows[1][1] * rows[2][0]);
    if s < 1e-8
        || lengths.iter().any(|n| (n - s).abs() > s * 1e-4)
        || det <= 0.
        || [(0, 1), (0, 2), (1, 2)]
            .iter()
            .any(|&(a, b)| dot(rows[a], rows[b]).abs() > s * s * 1e-4)
    {
        return Err("collision placement requires positive uniform scale without shear".into());
    }
    Ok(s as f32)
}
fn orientation(o: [f32; 9]) -> Result<(), String> {
    let mut m = IDENTITY;
    for r in 0..3 {
        for k in 0..3 {
            m[r * 4 + k] = o[r * 3 + k];
        }
    }
    if (scale(&m)? - 1.).abs() > 1e-4 {
        return Err("invalid primitive orientation".into());
    }
    Ok(())
}
pub fn box_mesh(center: [f32; 3], extent: [f32; 3], o: [f32; 9]) -> Result<Mesh, String> {
    orientation(o)?;
    if !center.iter().chain(&extent).all(|v| v.is_finite()) || extent.iter().any(|&v| v <= 0.) {
        return Err("invalid box collider".into());
    }
    let vertices = (0..8)
        .map(|i| {
            std::array::from_fn(|k| {
                center[k]
                    + (0..3)
                        .map(|r| {
                            extent[r]
                                * if i & (1 << r) == 0 {
                                    -o[r * 3 + k]
                                } else {
                                    o[r * 3 + k]
                                }
                        })
                        .sum::<f32>()
            })
        })
        .collect();
    let triangles = vec![
        [0, 2, 3],
        [0, 3, 1],
        [4, 5, 7],
        [4, 7, 6],
        [0, 1, 5],
        [0, 5, 4],
        [2, 6, 7],
        [2, 7, 3],
        [0, 4, 6],
        [0, 6, 2],
        [1, 3, 7],
        [1, 7, 5],
    ];
    Ok(Mesh {
        vertices,
        triangles,
    })
}
pub fn merge_meshes(surfaces: Vec<Surface>) -> Result<Mesh, String> {
    let mut out = Mesh::default();
    for surface in surfaces {
        let Surface::Mesh(m) = surface else {
            return Err("curved moving colliders need analytic underside sampling; use visual mode for this object".into());
        };
        let base =
            u32::try_from(out.vertices.len()).map_err(|_| "collision vertex count overflow")?;
        for t in m.triangles {
            out.triangles.push(t.map(|i| i + base));
        }
        out.vertices.extend(m.vertices);
    }
    if out.triangles.is_empty() {
        return Err("no enabled solid collider on the moving entity".into());
    }
    out.validate().map_err(|e| e.to_string())?;
    Ok(out)
}

/// Serialized flags are authoritative at native shape creation; XCOM material flags are authoring
/// defaults, not a second runtime filter. Dynamic/script-driven updates remain outside this reader.
pub fn surfaces(
    objects: &[&PropertyObject],
    strings: &[String],
    world: &[f32; 16],
    support: bool,
    region: Option<[f32; 6]>,
    mut load: impl FnMut(&str) -> Result<Arc<CollisionMesh>, String>,
) -> Result<Vec<Surface>, String> {
    let mut result = Vec::new();
    for ps in objects
        .iter()
        .filter(|o| o.class(strings) == "eCCollisionShape_PS")
    {
        let flag = |name| collision::checked_boolean(ps, strings, name).map_err(|e| e.to_string());
        if flag("DisableCollision")? == Some(true)
            || flag("DisableResponse")? == Some(true)
            || support && flag("IgnoredByTraceRay")? == Some(true)
        {
            continue;
        }
        for s in collision::shapes(ps, strings).map_err(|e| e.to_string())? {
            if s.disabled == Some(true)
                || s.response_disabled == Some(true)
                || support && s.ignored_by_ray == Some(true)
            {
                continue;
            }
            if s.kind == 0 || s.kind == 2 || s.kind == 7 {
                continue;
            } // no solid finite geometry created by native path
            if support && s.group.is_some_and(|g| !matches!(g, 1 | 2 | 9 | 10 | 20)) {
                continue;
            }
            if !support
                && s.group
                    .is_some_and(|g| matches!(g, 7 | 8 | 11 | 12 | 13 | 19))
            {
                continue;
            }
            let adapted = s.adapt_to_aabb == Some(true) || s.adapt_mode.is_some_and(|m| m != 0);
            if adapted {
                // An unsupported runtime-adapted shape must never disappear from a relevant query.
                // Its saved outer box can only exclude it when it is clearly outside the region.
                if let (Some(region), Some(bounds)) = (region, s.outer_box) {
                    if !intersects(crate::lrent::transform_box(bounds, world), region) {
                        continue;
                    }
                }
                return Err("runtime AABB-adapted collider is unsupported".into());
            }
            let uniform = world[0].hypot(world[1]).hypot(world[2]);
            let surface = if let Some(name) = s.mesh {
                let c = load(&crate::resource::compiled_name(&name, "_xcom"))?;
                let kind = if s.kind == 1 {
                    Kind::Triangle
                } else {
                    Kind::Convex
                };
                let m = c
                    .get(kind, s.resource_index.unwrap_or(0) as usize)
                    .ok_or("collision resource index outside mesh array")?;
                if region.is_some_and(|region| {
                    !intersects(crate::lrent::transform_box(m.bounds, world), region)
                }) {
                    continue;
                }
                scale(world)?;
                if s.kind == 1 && (uniform - 1.).abs() > 0.0001 {
                    return Err("native Risen cannot create scaled triangle colliders".into());
                }
                Surface::Mesh(m.mesh.transformed(world).map_err(|e| e.to_string())?)
            } else {
                if s.version < 74 {
                    return Err("legacy primitive collision pose is unsupported".into());
                }
                match s.primitive.ok_or("missing primitive collider geometry")? {
                    Primitive::Box {
                        center,
                        extent,
                        orientation,
                    } => Surface::Mesh(
                        box_mesh(center, extent, orientation)?
                            .transformed(world)
                            .map_err(|e| e.to_string())?,
                    ),
                    Primitive::Sphere { radius, center } => {
                        if !radius.is_finite()
                            || radius <= 0.
                            || !center.iter().all(|v| v.is_finite())
                        {
                            return Err("invalid sphere collider".into());
                        }
                        let center = point(center, world);
                        Surface::Capsule {
                            a: center,
                            b: center,
                            radius: radius * uniform,
                        }
                    }
                    Primitive::Capsule {
                        height,
                        radius,
                        orientation: o,
                        center,
                    } => {
                        orientation(o)?;
                        if !height.is_finite()
                            || height < 0.
                            || !radius.is_finite()
                            || radius <= 0.
                            || !center.iter().all(|v| v.is_finite())
                        {
                            return Err("invalid capsule collider".into());
                        }
                        let a = std::array::from_fn(|k| center[k] - o[3 + k] * height * 0.5);
                        let b = std::array::from_fn(|k| center[k] + o[3 + k] * height * 0.5);
                        Surface::Capsule {
                            a: point(a, world),
                            b: point(b, world),
                            radius: radius * uniform,
                        }
                    }
                }
            };
            if !surface.bounds().is_some_and(crate::bounds::valid_box) {
                return Err("invalid world collision bounds".into());
            }
            if region.is_some_and(|region| !intersects(surface.bounds().unwrap(), region)) {
                continue;
            }
            scale(world)?;
            result.push(surface);
        }
    }
    Ok(result)
}

fn intersects(a: [f32; 6], b: [f32; 6]) -> bool {
    (0..3).all(|k| a[k] <= b[k + 3] && a[k + 3] >= b[k])
}

pub fn bounds(surfaces: &[Surface]) -> Option<[f32; 6]> {
    let mut bounds = EMPTY_BOX;
    let mut any = false;
    for b in surfaces.iter().filter_map(Surface::bounds) {
        bounds = box_union(bounds, b);
        any = true;
    }
    any.then_some(bounds)
}
