//! Read-only compiled_meshes.bin corpus round trip; optional explicit scratch cache path.
use risen_formats::{archive::{Archive, StrRef}, bin_io::{Reader, Writer}, props::PropertyObject, vfs::GameVfs};
use std::{collections::BTreeMap, path::PathBuf};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let root = PathBuf::from(std::env::args_os().nth(1).ok_or("game root required")?);
    let vfs = GameVfs::open(&root)?;
    let bytes = match std::env::args_os().nth(2) {
        Some(path) => std::fs::read(path)?,
        None => vfs.read(vfs.find("data/common/meshes", "compiled_meshes.bin")?)?,
    };
    let archive = Archive::read(&bytes)?;
    let mut r = Reader::new(&archive.body);
    assert_eq!(r.bytes(4)?, b"GH04");
    let count = r.u32()?;
    let mut w = Writer::default(); w.bytes(b"GH04"); w.u32(count);
    let mut revisions = BTreeMap::new();
    let mut submeshes = BTreeMap::new();
    for _ in 0..count {
        let revision = r.u32()?;
        *revisions.entry(revision).or_insert(0usize) += 1;
        let object = PropertyObject::read(&mut r, &archive.strings)?;
        assert!(matches!((revision,object.class(&archive.strings)),
            (0x3230534d,"eCMeshResource2") | (0x30304d43,"eCCollisionMeshResource2")));
        let name = StrRef::read(&mut r)?;
        let flags = r.u32()?;
        let times = r.bytes(16)?;
        let body = object.body.as_ref().ok_or("empty mesh object")?;
        for p in &body.props.list {
            if archive.str(p.name) == "SubMeshes" {
                let mut s = Reader::new(&p.value);
                assert_eq!(s.u8()?, 1);
                let n = s.u32()?;
                *submeshes.entry(n).or_insert(0usize) += 1;
            }
        }
        w.u32(revision); object.write(&mut w); name.write(&mut w); w.u32(flags); w.bytes(times);
    }
    assert_eq!(r.remaining(), 0);
    assert_eq!(w.buf, archive.body);
    assert_eq!(archive.write(), bytes);
    println!("{count} cache headers: byte-identical object/body/archive round trip");
    println!("revisions={revisions:?}; submesh_counts={submeshes:?}");
    Ok(())
}
