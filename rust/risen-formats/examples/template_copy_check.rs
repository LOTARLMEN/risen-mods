//! Corpus check (from rust/):
//! cargo run -p risen-formats --example template_copy_check -- ../../data/common/templates.pak
//! Only reads the pak; verifies names, GUID uniqueness, string order, read/write, safe refusals,
//! and original bytes after undoing the intended differences (including duplicate string sites).

use std::collections::HashSet;
use risen_formats::{pak::Pak, tple::Tple};

fn main() {
    let pak = Pak::open(std::env::args().nth(1).expect("templates.pak path")).unwrap();
    let mut checked = 0;
    let mut refused = 0;
    let mut guids = HashSet::new();
    for entry in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
        let data = pak.read(entry).unwrap();
        let src = Tple::read(&data).unwrap();
        let Some(te) = src.template() else {
            assert!(src.copy_named("Copy_Test").is_err());
            refused += 1;
            continue;
        };
        assert!(src.copy_named(src.name(te)).is_err());
        assert!(src.copy_named("../Bad").is_err());
        assert!(src.copy_named("Bad.tple").is_err());
        assert!(src.copy_named("").is_err());
        let mut copied = match src.copy_named("It_Copy_Check") {
            Ok(t) => t,
            Err(e) => {
                println!("REFUSED {}: {e}", entry.path);
                refused += 1;
                continue;
            }
        };
        assert_eq!(src.write(), data, "source mutated: {}", entry.path);
        assert_eq!(copied.name(&copied.entities[0]), "It_Copy_Check.tple");
        assert_eq!(copied.name(copied.template().unwrap()), "It_Copy_Check");
        let bytes = copied.write();
        assert_eq!(Tple::read(&bytes).unwrap().write(), bytes);
        let mut first = Vec::new();
        assert!(copied.walk_strs(&mut |s| {
            if !first.contains(&s.0) { first.push(s.0); }
        }).is_empty());
        assert_eq!(first, (0..copied.strings().len() as u16).collect::<Vec<_>>());
        // Undo only the intended differences, then demand the ORIGINAL BYTES. This detects
        // accidental changes to resources, scripts, flags, dates, class data and properties.
        let pairs: Vec<_> = copied.entities.iter().zip(&src.entities).map(|(a, b)| {
            assert_ne!(a.id.guid, b.id.guid);
            assert!(guids.insert(a.id.guid));
            (a.id.guid, b.id.guid)
        }).collect();
        for (e, original) in copied.entities.iter_mut().zip(&src.entities) {
            e.id = original.id;
            for set in &mut e.sets {
                for (new, old) in &pairs { set.object.replace_guid(new, old); }
            }
        }
        for i in 0..2 {
            copied.entities[i].name = copied.archive.intern(src.name(&src.entities[i]));
        }
        let strings = copied.strings().to_vec();
        // Some shipped tables contain duplicate strings. Restore the index at each original
        // site, rather than choosing an arbitrary index of an equal string.
        let mut indices = Vec::new();
        assert!(src.clone().walk_strs(&mut |s| indices.push(s.0)).is_empty());
        let mut site = 0;
        assert!(copied.walk_strs(&mut |s| {
            let original = indices[site];
            assert_eq!(strings[s.0 as usize], src.strings()[original as usize]);
            s.0 = original;
            site += 1;
        }).is_empty());
        assert_eq!(site, indices.len());
        copied.archive.strings = src.strings().to_vec();
        let restored = copied.write();
        if restored != data {
            let at = restored.iter().zip(&data).position(|(a, b)| a != b).unwrap_or(restored.len().min(data.len()));
            panic!("unexpected copy difference: {}, first offset {at:#x}, lengths {} / {}, bytes {:?} / {:?}",
                entry.path, restored.len(), data.len(), restored.get(at..at+12), data.get(at..at+12));
        }
        checked += 1;
    }
    println!("template copy: {checked} exact after undoing names/GUIDs; {refused} refused");
    assert!(checked > 2500);
}
