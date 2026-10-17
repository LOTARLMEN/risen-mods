//! Independently read scratch visual/collision resources and compare with native PhysX buffers.
use risen_formats::{bin_io::Reader, geometry::Mesh, resource, xcom::CollisionMesh};
use std::path::PathBuf;

fn triangles(mesh: &Mesh) -> Vec<Vec<[i64; 3]>> {
    let mut result: Vec<_> = mesh.triangles.iter().map(|f| {
        let mut points: Vec<_> = f.iter().map(|&i| mesh.vertices[i as usize].map(|v| (v as f64 * 1000.).round() as i64)).collect();
        points.sort(); points
    }).collect();
    result.sort(); result
}

fn check_native_render_winding(bytes: &[u8]) -> Result<(),Box<dyn std::error::Error>> {
    let at=u32::from_le_bytes(bytes[16..20].try_into()?) as usize;
    let mut r=Reader::new(&bytes[at..]);let decl=r.bytes(128)?;
    let find=|usage|decl.chunks_exact(8).find(|d|d[..2]==[0,0] && d[4]==2 && d[6]==usage)
        .map(|d|u16::from_le_bytes([d[2],d[3]]) as usize).ok_or("position/normal declaration missing");
    let position=find(0)?;let normal=find(3)?;
    let nbytes=r.u32()? as usize;r.bytes(12)?;let stride=r.u32()? as usize;r.bytes(40)?;
    let ibytes=r.u32()? as usize;r.bytes(12)?;let stream=r.bytes(nbytes)?;
    for _ in 0..ibytes/12 {
        let ids=[r.u32()?,r.u32()?,r.u32()?];
        let p=ids.map(|i|Reader::new(&stream[i as usize*stride+position..]).f32s::<3>()).into_iter().collect::<Result<Vec<_>,_>>()?;
        let n=ids.map(|i|Reader::new(&stream[i as usize*stride+normal..]).f32s::<3>()).into_iter().collect::<Result<Vec<_>,_>>()?;
        let a:[f64;3]=std::array::from_fn(|k|f64::from(p[1][k]-p[0][k]));
        let b:[f64;3]=std::array::from_fn(|k|f64::from(p[2][k]-p[0][k]));
        let cross=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
        let dot:f64=(0..3).map(|k|cross[k]*n.iter().map(|v|f64::from(v[k])).sum::<f64>()).sum();
        assert!(dot < -1e-9,"render face is reversed relative to the shipped XMSH convention");
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let dir = PathBuf::from(std::env::args_os().nth(1).ok_or("scratch directory required")?);
    check_native_render_winding(&std::fs::read(dir.join("donor._xmsh"))?)?;
    for (suffix, height, dimensions) in [
        ("Ramp", 40., [200., 80., 200.]),
        ("Stretch", 20., [300., 40., 200.]),
        ("Trim", 20., [200., 40., 100.]),
        ("Terrain", 35., [200., 35., 200.]),
    ] {
        let name = format!("RisenMods_Probe_{suffix}");
        let bytes = std::fs::read(dir.join(format!("{name}._xmsh")))?;
        check_native_render_winding(&bytes)?;
        let mesh = Mesh::read_xmsh(&bytes)?;
        assert_eq!(mesh.triangles.len(), 8);
        assert_eq!(mesh.vertices.len(), 24);
        let b = mesh.bounds().ok_or("empty visual mesh")?;
        assert_eq!(resource::boundary(&bytes), Some(b));
        assert_eq!([b[3]-b[0], b[4]-b[1], b[5]-b[2]], dimensions);
        let xcom = CollisionMesh::read(&std::fs::read(dir.join(format!("{name}._xcom")))?)?;
        assert_eq!(xcom.meshes.len(), 1);
        assert_eq!(triangles(&mesh), triangles(&xcom.meshes[0].mesh));
        let native = std::fs::read(dir.join(format!("{name}._xcom.native")))?;
        let mut r = Reader::new(&native);
        assert_eq!(r.bytes(8)?, b"XCOMREF1"); assert_eq!(r.u32()?, 1);
        let cooked = &xcom.meshes[0];
        assert_eq!(r.u32()? as usize, cooked.stream_start);
        assert_eq!(r.u32()? as usize, cooked.stream_end);
        assert_eq!(r.u32()? as usize, cooked.mesh.vertices.len());
        assert_eq!(r.u32()? as usize, cooked.mesh.triangles.len());
        for p in &cooked.mesh.vertices { assert_eq!(r.f32s::<3>()?.map(|v| v*100.), *p); }
        for f in &cooked.mesh.triangles { assert_eq!([r.u32()?,r.u32()?,r.u32()?], *f); }
        assert_eq!(r.u32()?, 0);
        assert_eq!(r.u32()? as usize, xcom.materials_offset);
        assert_eq!(r.remaining(), 0);
        let z = if suffix == "Terrain" { 0. } else { dimensions[2]/2. };
        for m in [&mesh, &cooked.mesh] {
            let actual = m.surface_y(0., z, -100., 100.).ok_or("missing top surface")?;
            assert!((actual-height).abs()<0.001, "{name}: {actual} != {height}");
        }
        println!("{name}: render/native/Rust triangle surfaces agree, expected top={height} cm");
    }
    Ok(())
}
