//! Read-only corpus checks for geometry and template-bound contributions.
use risen_formats::{geometry::{Mesh, actor_nodes, actor_meshes}, resource, pak::Pak};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(std::env::args().nth(1).ok_or("mesh/animation pak")?)?;
    let limit = std::env::args().nth(2).and_then(|s| s.parse::<usize>().ok()).unwrap_or(usize::MAX);
    let mut ok = 0;
    let mut failed = std::collections::BTreeMap::<String, usize>::new();
    let mut vertices = 0;
    let mut triangles = 0;
    let mut nodes = 0;
    for e in pak.entries.iter().filter(|e| e.path.ends_with("._xmsh") || e.path.ends_with("._xmac")).take(limit) {
        let bytes = pak.read(e)?;
        let result = if e.path.ends_with("._xmsh") {
            Mesh::read_xmsh(&bytes).map(|m| {
                vertices += m.vertices.len(); triangles += m.triangles.len();
                if let (Some(b), Some(resource_box)) = (m.bounds(), resource::boundary(&bytes)) {
                    for k in 0..3 { assert!(b[k] >= resource_box[k] - 0.1 && b[3 + k] <= resource_box[3 + k] + 0.1, "{}", e.path); }
                }
            })
        } else {
            actor_nodes(&bytes).and_then(|n| {
                nodes += n.len();
                for (_, mesh) in actor_meshes(&bytes)? { vertices += mesh.vertices.len(); triangles += mesh.triangles.len(); }
                Ok(())
            })
        };
        match result {
            Ok(()) => ok += 1,
            Err(err) => {
                if failed.len() < 8 { println!("{}: {err}", e.path); }
                *failed.entry(err.msg).or_default() += 1;
            }
        }
    }
    println!("{ok} parsed; {vertices} vertices, {triangles} triangles, {nodes} actor nodes; refusals: {failed:?}");
    Ok(())
}
