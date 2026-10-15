//! Validate empty layer construction against every shipped layer; writes nothing.
use risen_formats::lrent::{Lrent, EMPTY_BOX};
use risen_formats::pak::Pak;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(std::env::args().nth(1).ok_or("usage: layer_create_check projects.pak")?)?;
    let mut ok = 0;
    let mut guids = std::collections::HashSet::new();
    for e in pak.entries.iter().filter(|e| e.path.to_ascii_lowercase().ends_with(".lrent")) {
        let bytes = pak.read(e)?;
        let donor = Lrent::read(&bytes)?;
        let mut empty = donor.empty_like().map_err(|err| format!("{}: {err}", e.path))?;
        let out = empty.write();
        let back = Lrent::read(&out)?;
        let c = back.context().ok_or("no context")?;
        assert_eq!(c.entities.len(), 1);
        assert_eq!(c.hierarchy, [(-1, -1)]);
        assert_eq!(c.entities[0].geo_box, EMPTY_BOX);
        assert!(c.entities[0].geo_sphere.iter().all(|v| v.is_finite()));
        assert_ne!(c.entities[0].id.guid, donor.entities()[0].id.guid);
        assert!(guids.insert(c.entities[0].id.guid));
        assert_eq!(back.write(), out);
        assert_eq!(donor.write(), bytes);
        let mut sequence = Vec::new();
        assert!(empty.walk_strs(&mut |s| sequence.push(s.0)).is_empty());
        let mut seen = std::collections::HashSet::new();
        let first: Vec<_> = sequence.into_iter().filter(|&i| seen.insert(i)).collect();
        assert_eq!(first, (0..back.strings().len() as u16).collect::<Vec<_>>());
        ok += 1;
    }
    println!("{ok} empty layers: new unique GUIDs, one root, finite bounds, exact string table, readback OK; donors unchanged");
    Ok(())
}
