//! Read the entire collision corpus and optionally compare every byte of native geometry.
//! cargo run -p risen-formats --example xcom_check -- <physics.pak> [native-reference-directory]
//! Or use <physics.pak> --extract <new-directory> to prepare input for tools/xcom-native-check.cpp.
use risen_formats::{
    bin_io::Reader,
    pak::Pak,
    xcom::{CollisionMesh, Kind},
};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(std::env::args().nth(1).ok_or("physics.pak")?)?;
    let mode = std::env::args().nth(2);
    let extraction = if mode.as_deref() == Some("--extract") {
        Some(std::path::PathBuf::from(
            std::env::args().nth(3).ok_or("extraction directory")?,
        ))
    } else {
        None
    };
    let reference = if extraction.is_some() { None } else { mode };
    if let Some(dir) = &extraction {
        std::fs::create_dir_all(dir)?;
    }
    let mut files = 0;
    let mut meshes = 0;
    let mut vertices = 0;
    let mut triangles = 0;
    let mut errors = 0;
    for entry in pak.entries.iter().filter(|e| e.path.ends_with("._xcom")) {
        let bytes = pak.read(entry)?;
        if let Some(dir) = &extraction {
            use std::io::Write;
            let name = std::path::Path::new(&entry.path)
                .file_name()
                .ok_or("missing resource filename")?;
            std::fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(dir.join(name))?
                .write_all(&bytes)?;
        }
        let resource = match CollisionMesh::read(&bytes) {
            Ok(r) => r,
            Err(e) => {
                eprintln!("{}: {e}", entry.path);
                errors += 1;
                continue;
            }
        };
        if let Some(dir) = &reference {
            let name = std::path::Path::new(&entry.path)
                .file_name()
                .unwrap()
                .to_str()
                .unwrap();
            let bytes = std::fs::read(std::path::Path::new(dir).join(format!("{name}.native")))?;
            let mut r = Reader::new(&bytes);
            assert_eq!(r.bytes(8)?, b"XCOMREF1");
            for kind in [Kind::Triangle, Kind::Convex] {
                let n = r.u32()? as usize;
                assert_eq!(
                    n,
                    resource.meshes.iter().filter(|m| m.kind == kind).count(),
                    "{}",
                    entry.path
                );
                for i in 0..n {
                    let m = resource.get(kind, i).unwrap();
                    assert_eq!(r.u32()? as usize, m.stream_start, "{} start", entry.path);
                    assert_eq!(r.u32()? as usize, m.stream_end, "{} end", entry.path);
                    assert_eq!(
                        r.u32()? as usize,
                        m.mesh.vertices.len(),
                        "{} vertices",
                        entry.path
                    );
                    assert_eq!(
                        r.u32()? as usize,
                        m.mesh.triangles.len(),
                        "{} triangles",
                        entry.path
                    );
                    for p in &m.mesh.vertices {
                        for &v in p {
                            assert_eq!(
                                (r.f32()? * 100.).to_bits(),
                                v.to_bits(),
                                "{} coordinate",
                                entry.path
                            );
                        }
                    }
                    for t in &m.mesh.triangles {
                        for &v in t {
                            assert_eq!(r.u32()?, v, "{} index", entry.path);
                        }
                    }
                }
            }
            assert_eq!(
                r.u32()? as usize,
                resource.materials_offset,
                "{} materials offset",
                entry.path
            );
            assert_eq!(r.remaining(), 0);
        }
        for m in &resource.meshes {
            let b = m.mesh.bounds().unwrap();
            for k in 0..3 {
                assert!(
                    b[k] >= m.bounds[k] - 0.1 && b[k + 3] <= m.bounds[k + 3] + 0.1,
                    "{} cooked bounds",
                    entry.path
                );
            }
            meshes += 1;
            vertices += m.mesh.vertices.len();
            triangles += m.mesh.triangles.len();
        }
        files += 1;
    }
    println!(
        "{files} resources, {meshes} meshes, {vertices} vertices, {triangles} triangles; {errors} refusals; native comparison {}",
        if reference.is_some() {
            "enabled"
        } else {
            "disabled"
        }
    );
    if errors > 0 {
        return Err("collision corpus refusals".into());
    }
    Ok(())
}
