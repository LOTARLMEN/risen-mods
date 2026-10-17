//! Placement math shared by the CLI and future editor. Does not simulate rigid bodies.
use crate::bounds::valid_box;
use crate::geometry::Mesh;
use crate::lrent::transform_vector;

#[derive(Debug, Clone, Copy)]
pub enum Side { Top, Bottom, Left, Right, Front, Back }

/// Adjacent world AABB faces. Positive gap separates, negative gap intentionally embeds objects.
/// With center=false only the face-normal coordinate changes.
pub fn snap(a: [f32; 6], b: [f32; 6], side: Side, gap: f32, center: bool) -> Result<[f32; 3], String> {
    if !valid_box(a) || !valid_box(b) || !gap.is_finite() { return Err("snap needs finite ordered bounds and gap".into()); }
    let (axis, positive) = match side { Side::Top => (1, true), Side::Bottom => (1, false), Side::Right => (0, true),
        Side::Left => (0, false), Side::Front => (2, true), Side::Back => (2, false) };
    let mut d = [0.; 3];
    d[axis] = if positive { b[3 + axis] + gap - a[axis] } else { b[axis] - gap - a[3 + axis] };
    if center { for k in 0..3 { if k != axis { d[k] = b[k] / 2. + b[3 + k] / 2. - a[k] / 2. - a[3 + k] / 2.; } } }
    if !d.iter().all(|v| v.is_finite()) { return Err("snap offset overflows".into()); }
    Ok(d)
}

pub fn footprint(b: [f32; 6]) -> Result<[[f32; 2]; 5], String> {
    if !valid_box(b) { return Err("invalid placement bounds".into()); }
    Ok([[b[0] / 2. + b[3] / 2., b[2] / 2. + b[5] / 2.], [b[0], b[2]], [b[3], b[2]], [b[0], b[5]], [b[3], b[5]]])
}

/// Five-point visual-surface placement. Every footprint sample must find support; choose the
/// highest support to avoid sampled penetration. This is not exact mesh-vs-mesh contact on slopes.
pub fn surface_offset(b: [f32; 6], surfaces: &[Mesh], drop: f32, rise: f32, gap: f32) -> Result<f32, String> {
    surface_offset_on(b,surfaces,drop,rise,gap)
}
pub fn surface_offset_on<S: crate::collider::VerticalSurface>(b: [f32; 6], surfaces: &[S], drop: f32, rise: f32, gap: f32) -> Result<f32, String> {
    if ![drop, rise, gap].iter().all(|v| v.is_finite()) || drop < 0. || rise < 0. {
        return Err("placement distances must be finite; drop/rise must be nonnegative".into());
    }
    let samples = footprint(b)?;
    let bottom = b[1] - drop;
    let top = b[1] + rise;
    if !bottom.is_finite() || !top.is_finite() { return Err("placement search overflows".into()); }
    let mut highest = f32::NEG_INFINITY;
    for [x, z] in samples {
        let hit = surfaces.iter().filter_map(|m| m.surface_y(x, z, bottom, top)).max_by(f32::total_cmp)
            .ok_or_else(|| format!("no support at ({x}, {z}) in height range {bottom}..{top}"))?;
        highest = highest.max(hit);
    }
    let d = highest + gap - b[1];
    if !d.is_finite() { return Err("surface offset overflows".into()); }
    Ok(d)
}

#[derive(Debug)]
pub struct GroundPlan {
    pub world_matrix: [f32; 16],
    pub normal: [f32; 3],
    pub slope_degrees: f32,
    pub samples: usize,
    /// Largest remaining vertical clearance among sampled model points (relative to requested gap).
    pub max_clearance: f32,
}

fn unit(v: [f64; 3]) -> Result<[f64; 3], String> {
    let length = v.iter().map(|v| v * v).sum::<f64>().sqrt();
    if !length.is_finite() || length < 1e-12 { return Err("degenerate placement direction".into()); }
    Ok(v.map(|v| v / length))
}
fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
}
fn point(p: [f32; 3], m: &[f32; 16]) -> [f32; 3] {
    let v = transform_vector(p, m);
    std::array::from_fn(|k| v[k] + m[12+k])
}

/// Least-squares height plane in centered f64 coordinates, stable far from the world origin.
fn plane_normal(points: &[[f32; 3]]) -> Result<[f64; 3], String> {
    let n = points.len() as f64;
    let mean: [f64; 3] = std::array::from_fn(|k| points.iter().map(|p| p[k] as f64).sum::<f64>() / n);
    let (mut xx, mut zz, mut xz, mut xy, mut zy) = (0., 0., 0., 0., 0.);
    for p in points {
        let [x, y, z] = std::array::from_fn(|k| p[k] as f64 - mean[k]);
        xx += x*x; zz += z*z; xz += x*z; xy += x*y; zy += z*y;
    }
    let det = xx*zz-xz*xz;
    if !det.is_finite() || det <= (xx*zz).abs()*1e-10 || xx <= 0. || zz <= 0. {
        return Err("support footprint cannot define a surface plane".into());
    }
    unit([-(xy*zz-zy*xz)/det, 1., -(zy*xx-xy*xz)/det])
}

/// Dense visual placement. Sample a local-XZ grid on the mesh's lowest vertical surface,
/// plus its vertices. Optional tilt fits a terrain plane, retaining horizontal heading and scale.
/// Final vertical shift prevents penetration at the sampled points; this is not PhysX contact.
pub fn ground_plan(mesh: &Mesh, world: [f32; 16], surfaces: &[Mesh], plane: Option<f32>,
    drop: f32, rise: f32, gap: f32, align: bool, samples: usize, max_slope: f32) -> Result<GroundPlan, String>
{
    ground_plan_on(mesh,world,surfaces,plane,drop,rise,gap,align,samples,max_slope)
}
pub fn ground_plan_on<S: crate::collider::VerticalSurface>(mesh: &Mesh, world: [f32; 16], surfaces: &[S], plane: Option<f32>,
    drop: f32, rise: f32, gap: f32, align: bool, samples: usize, max_slope: f32) -> Result<GroundPlan, String>
{
    mesh.validate().map_err(|e| e.to_string())?;
    if !world.iter().all(|v| v.is_finite()) || [world[3], world[7], world[11]] != [0.,0.,0.] || world[15] <= 0.
        || ![drop,rise,gap,max_slope].iter().all(|v| v.is_finite()) || drop < 0. || rise < 0.
        || !(3..=17).contains(&samples) || !(0.0..90.0).contains(&max_slope)
        || plane.is_some_and(|v| !v.is_finite()) {
        return Err("invalid dense grounding inputs".into());
    }
    let bounds = mesh.bounds().ok_or("empty placement mesh")?;
    if mesh.triangles.is_empty() { return Err("placement mesh has no triangles".into()); }
    let mut lower = Vec::new();
    for p in &mesh.vertices {
        if let Some((y,_))=mesh.vertical_range(p[0],p[2],-f32::MAX,f32::MAX) { lower.push([p[0],y,p[2]]); }
    }
    for ix in 0..samples { for iz in 0..samples {
        let x = bounds[0] + (bounds[3]-bounds[0]) * ix as f32 / (samples-1) as f32;
        let z = bounds[2] + (bounds[5]-bounds[2]) * iz as f32 / (samples-1) as f32;
        if let Some((y, _)) = mesh.vertical_range(x, z, -f32::MAX, f32::MAX) { lower.push([x,y,z]); }
    } }
    lower.sort_by(|a,b| a[0].total_cmp(&b[0]).then(a[1].total_cmp(&b[1])).then(a[2].total_cmp(&b[2])));
    lower.dedup();
    if lower.len()<3 { return Err("not enough underside samples".into()); }
    let mut local = mesh.vertices.clone();
    local.extend(&lower);
    local.sort_by(|a,b| a[0].total_cmp(&b[0]).then(a[1].total_cmp(&b[1])).then(a[2].total_cmp(&b[2])));
    local.dedup();
    if local.len() < 3 { return Err("not enough model support samples".into()); }
    let original_bounds = mesh.transformed(&world).map_err(|e| e.to_string())?.bounds().unwrap();
    ground_sampled_plan(world, original_bounds, surfaces, plane, drop, rise, gap, align, max_slope,
        |pose| Ok((local.iter().map(|&p| point(p,pose)).collect(), lower.iter().map(|&p| point(p,pose)).collect())))
}

/// Physical grounding with analytic curved moving shapes. Polygon-only bodies keep the existing
/// mesh sampling path. Curved bodies are resampled vertically after each change in orientation.
/// This still samples the footprint; it does not provide continuous contact or simulate PhysX.
pub fn ground_colliders_on<S: crate::collider::VerticalSurface>(body: &[crate::collider::Surface],
    world: [f32;16], surfaces: &[S], plane: Option<f32>, drop:f32, rise:f32, gap:f32,
    align:bool, samples:usize, max_slope:f32) -> Result<GroundPlan,String>
{
    use crate::collider::{self, Surface};
    if body.is_empty() { return Err("no enabled solid collider on the moving entity".into()); }
    if body.iter().all(|s| matches!(s, Surface::Mesh(_))) {
        return ground_plan_on(&collider::merge_meshes(body.to_vec())?, world, surfaces, plane,
            drop,rise,gap,align,samples,max_slope);
    }
    if !(3..=17).contains(&samples) || ![drop,rise,gap,max_slope].iter().all(|v|v.is_finite())
        || drop < 0. || rise < 0. || !(0.0..90.0).contains(&max_slope)
        || plane.is_some_and(|v|!v.is_finite()) {
        return Err("invalid dense grounding inputs".into());
    }
    let transformed = body.iter().map(|s|s.transformed(&world)).collect::<Result<Vec<_>,_>>()?;
    let bounds = collider::bounds(&transformed).ok_or("empty moving collider")?;
    ground_sampled_plan(world,bounds,surfaces,plane,drop,rise,gap,align,max_slope,
        |pose| collider_samples(body,pose,samples))
}

type GroundSamples = (Vec<[f32;3]>, Vec<[f32;3]>);

fn collider_samples(body:&[crate::collider::Surface], pose:&[f32;16], samples:usize)
    -> Result<GroundSamples,String>
{
    use crate::collider::Surface;
    let mut lower = Vec::new();
    let mut all = Vec::new();
    for local in body {
        let shape = local.transformed(pose)?;
        let bounds = shape.bounds().ok_or("empty moving collider")?;
        let mut xz = Vec::new();
        for ix in 0..samples { for iz in 0..samples {
            xz.push([bounds[0]+(bounds[3]-bounds[0])*ix as f32/(samples-1) as f32,
                bounds[2]+(bounds[5]-bounds[2])*iz as f32/(samples-1) as f32]);
        } }
        match &shape {
            Surface::Mesh(m) => {
                xz.extend(m.vertices.iter().map(|p|[p[0],p[2]]));
                all.extend(&m.vertices);
            }
            Surface::Capsule { a,b,radius } => {
                // Segment stations and a circular silhouette keep long/thin or offset capsules
                // represented even when an AABB grid misses most of their projected footprint.
                // End centers also include the exact global bottom/top of the rounded shape.
                for i in 0..samples {
                    let t=i as f32/(samples-1) as f32;
                    let center:[f32;3]=std::array::from_fn(|k|a[k]+(b[k]-a[k])*t);
                    xz.push([center[0],center[2]]);
                    for j in 0..16 {
                        let angle=j as f32*std::f32::consts::TAU/16.;
                        // One interior ring and the silhouette; intersections remain analytic.
                        for fraction in [0.5,1.] {
                            xz.push([center[0]+radius*fraction*angle.cos(),center[2]+radius*fraction*angle.sin()]);
                        }
                    }
                }
            }
        }
        for [x,z] in xz {
            if let Some((low,high))=shape.vertical_range(x,z,-f32::MAX,f32::MAX) {
                lower.push([x,low,z]);
                all.push([x,low,z]);
                all.push([x,high,z]);
            }
        }
    }
    for points in [&mut all,&mut lower] {
        points.sort_by(|a,b|a[0].total_cmp(&b[0]).then(a[1].total_cmp(&b[1])).then(a[2].total_cmp(&b[2])));
        points.dedup();
        if points.len()<3 { return Err("not enough physical underside samples".into()); }
    }
    Ok((all,lower))
}

fn ground_sampled_plan<S: crate::collider::VerticalSurface>(world:[f32;16],original_bounds:[f32;6],
    surfaces:&[S],plane:Option<f32>,drop:f32,rise:f32,gap:f32,align:bool,max_slope:f32,
    sample:impl Fn(&[f32;16])->Result<GroundSamples,String>) -> Result<GroundPlan,String>
{
    let bottom = original_bounds[1]-drop;
    let top = original_bounds[1]+rise;
    if !bottom.is_finite() || !top.is_finite() { return Err("ground search overflows".into()); }
    let terrain = |p: [f32;3]| -> Result<[f32;3],String> {
        let y = if let Some(y) = plane { y } else {
            surfaces.iter().filter_map(|m| m.surface_y(p[0],p[2],bottom,top)).max_by(f32::total_cmp)
                .ok_or_else(|| format!("no support at ({}, {}) in height range {bottom}..{top}",p[0],p[2]))?
        };
        Ok([p[0],y,p[2]])
    };
    let mut target = world;
    let mut normal = [0.,1.,0.];
    if align {
        let rows: [[f64;3];3] = std::array::from_fn(|r| std::array::from_fn(|k| world[4*r+k] as f64));
        let scale: [f64;3] = std::array::from_fn(|r| rows[r].iter().map(|v|v*v).sum::<f64>().sqrt());
        let axes = [unit(rows[0])?,unit(rows[1])?,unit(rows[2])?];
        let dot = |a: [f64;3],b: [f64;3]| (0..3).map(|k|a[k]*b[k]).sum::<f64>();
        if dot(axes[0],axes[1]).abs()>1e-4 || dot(axes[0],axes[2]).abs()>1e-4
            || dot(axes[1],axes[2]).abs()>1e-4 || dot(cross(axes[0],axes[1]),axes[2])<0.9999 {
            return Err("slope alignment requires positive scale without shear".into());
        }
        let heading = if rows[2][0].hypot(rows[2][2]) > 1e-6 { [rows[2][0],0.,rows[2][2]] }
            else { [-rows[0][2],0.,rows[0][0]] };
        let heading = unit(heading)?;
        let mut converged = false;
        for _ in 0..12 {
            let (_,lower)=sample(&target)?;
            let hits: Vec<_> = lower.iter().map(|&p| terrain(p)).collect::<Result<_,_>>()?;
            let next = plane_normal(&hits)?;
            let slope = next[1].clamp(-1.,1.).acos().to_degrees();
            if slope > max_slope as f64 { return Err(format!("surface slope {slope:.2} exceeds limit {max_slope} degrees")); }
            let forward = unit([heading[0],-(next[0]*heading[0]+next[2]*heading[2])/next[1],heading[2]])?;
            let right = unit(cross(next,forward))?;
            let old: [f32;3] = std::array::from_fn(|k| target[4+k]/scale[1] as f32);
            for (r,axis) in [right,next,forward].iter().enumerate() {
                for k in 0..3 { target[4*r+k]=(axis[k]*scale[r]) as f32; }
            }
            normal=next;
            if (0..3).map(|k|(old[k] as f64-next[k]).powi(2)).sum::<f64>() < 1e-8 { converged=true; break; }
        }
        if !converged { return Err("surface tilt did not converge; use a smaller object or explicit plane".into()); }
    }
    let (local,_)=sample(&target)?;
    let mut delta = f32::NEG_INFINITY;
    for &p in &local {
        let difference=terrain(p)?[1]+gap-p[1];
        delta=delta.max(difference);
    }
    if !delta.is_finite() || delta < -drop || delta > rise { return Err("ground displacement exceeds search limits".into()); }
    target[13]+=delta;
    if !target.iter().all(|v|v.is_finite()) { return Err("ground pose overflows".into()); }
    let mut clearance = 0.0f32;
    let (_,lower)=sample(&target)?;
    for &p in &lower {
        clearance=clearance.max(p[1]-terrain(p)?[1]-gap);
    }
    Ok(GroundPlan { world_matrix: target, normal: normal.map(|v|v as f32),
        slope_degrees: normal[1].clamp(-1.,1.).acos().to_degrees() as f32,
        samples: local.len(), max_clearance: clearance })
}
