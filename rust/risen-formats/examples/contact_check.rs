//! Read-only nearest-distance/intersection checks against every cooked collision mesh.
use risen_formats::{
    collider::{Shape, Surface},
    contact::{Body, query},
    lrent::IDENTITY,
    pak::Pak,
    xcom::{CollisionMesh, Kind},
};
fn ball(center: [f32; 3]) -> Body {
    Body::new(
        &[Shape {
            surface: Surface::Capsule {
                a: center,
                b: center,
                radius: 1.,
            },
            solid: true,
        }],
        IDENTITY,
    )
    .unwrap()
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(std::env::args().nth(1).ok_or("physics.pak required")?)?;
    let mut resources = 0;
    let mut meshes = 0;
    let mut convex = 0;
    for entry in pak.entries.iter().filter(|e| e.path.ends_with("._xcom")) {
        let resource = CollisionMesh::read(&pak.read(entry)?)?;
        resources += 1;
        for mesh in resource.meshes {
            let solid = mesh.kind == Kind::Convex;
            let p = mesh
                .mesh
                .triangles
                .iter()
                .flatten()
                .map(|&i| mesh.mesh.vertices[i as usize])
                .max_by(|a, b| a[0].total_cmp(&b[0]))
                .ok_or("empty cooked mesh")?;
            let body = Body::new(
                &[Shape {
                    surface: Surface::Mesh(mesh.mesh),
                    solid,
                }],
                IDENTITY,
            )
            .map_err(|e| format!("{} {:?}/{}: {e}", entry.path, mesh.kind, mesh.index))?;
            // At the maximal-X referenced vertex, an outward X displacement has a known exact
            // nearest point: no polygon can extend further along X. This is independent of BVH.
            let mut center = p;
            center[0] += 20.;
            let expected = center[0] as f64 - p[0] as f64 - 1.;
            let separated = query(&body, &ball(center));
            assert!(
                (separated.distance_cm - expected).abs() < 1e-6,
                "{} {:?}: {:?}, expected {expected}",
                entry.path,
                mesh.kind,
                separated
            );
            assert!(!separated.intersects);
            let touching = query(&body, &ball(p));
            assert!(touching.intersects, "{} {:?}", entry.path, mesh.kind);
            meshes += 1;
            convex += solid as usize;
        }
    }
    println!(
        "{resources} resources; {meshes} meshes ({convex} convex solids); {} analytic-distance/overlap checks passed",
        meshes * 2
    );
    Ok(())
}
