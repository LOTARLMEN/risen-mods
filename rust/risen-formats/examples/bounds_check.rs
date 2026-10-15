//! Compare resource-only and property-set bounds against shipped world instances; reads only.
use std::collections::{HashMap, HashSet, BTreeMap};
use risen_formats::{bounds::{object_bounds, template_bounds}, lrent::{Lrent, Creator}, pak::Pak, tple::Tple,
    resource::{boundary, compiled_name}, vfs::{GameVfs, VfsError}};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let root = std::env::args().nth(1).ok_or("game directory")?;
    let vfs = GameVfs::open(&root)?;
    let templates = Pak::open(std::path::Path::new(&root).join("data/common/templates.pak"))?;
    let projects = Pak::open(std::path::Path::new(&root).join("data/common/projects.pak"))?;
    let mut ts = HashMap::new();
    for e in templates.entries.iter().filter(|e| e.path.ends_with(".tple")) {
        let t = Tple::read(&templates.read(e)?)?;
        if let Some(e) = t.template() { ts.insert(e.id.guid, t); }
    }
    let mut cache = HashMap::<String, Option<([f32; 6], String)>>::new();
    let mut load = |scope: &str, name: &str| -> Result<_, String> {
        let key = format!("{scope}/{name}");
        if let Some(b) = cache.get(&key) { return Ok(b.clone()); }
        let b = match vfs.find(scope, name) {
            Ok(e) => boundary(&vfs.read(e).map_err(|e| e.to_string())?).map(|b| (b, e.path.clone())),
            Err(VfsError::NotFound(_)) => None,
            Err(e) => return Err(e.to_string()),
        };
        cache.insert(key, b.clone()); Ok(b)
    };
    let close = |a: [f32; 6], b: [f32; 6]| a.iter().zip(b).all(|(a, b)| (a - b).abs() <= 0.01 * (1. + a.abs()));
    let mut seen = HashSet::new(); let mut counts = [0; 5]; let mut classes = BTreeMap::<String, usize>::new();
    for pe in projects.entries.iter().filter(|e| e.path.ends_with(".lrent")) {
        let l = Lrent::read(&projects.read(pe)?)?;
        let c = l.context().unwrap();
        for (i, e) in c.entities.iter().enumerate().skip(1) {
            let Creator::Proxy(p) = &e.creator else { continue };
            let Some(id) = p.id else { continue };
            let Some(t) = ts.get(&id.guid) else { continue };
            if c.parent(i) != Some(0) || !seen.insert(id.guid) { continue; }
            let mut old = [-50., -50., -50., 50., 50., 50.];
            let te = t.template().unwrap();
            for (class, prop, scope, ext) in [("eCMesh_PS", "MeshFileName", "data/common/meshes", "_xmsh"),
                ("eCAnimation_PS", "ResourceFilePath", "data/compiled/animations", "_xmac")] {
                if let Some(o) = te.sets.iter().find(|s| s.object.class(t.strings()) == class) {
                    if let Some(f) = risen_formats::bounds::string_value(&o.object, t.strings(), prop) {
                        if let Some((b, _)) = load(scope, &compiled_name(f, ext))? { old = b; break; }
                    }
                }
            }
            let b = template_bounds(t, &mut load)?;
            let instance = object_bounds(&e.property_sets.iter().map(|s| &s.object).collect::<Vec<_>>(), l.strings(), &mut load)?;
            let old_ok = close(e.local_box, old); let new_ok = close(e.local_box, b.local_box);
            counts[0] += old_ok as usize; counts[1] += new_ok as usize; counts[2] += close(e.local_box, instance.local_box) as usize;
            if !old_ok && new_ok { counts[3] += 1; }
            if old_ok && !new_ok {
                counts[4] += 1;
                println!("changed match {}: stored {:?}; template {:?}; instance {:?}; sources {:?}", t.name(te), e.local_box,
                    b.local_box, instance.local_box, b.sources);
            }
            if !new_ok {
                let class = e.set_classes(l.strings()).into_iter().filter(|c| ["eCAnimation_PS", "eCSpeedTree_PS", "eCParticle_PS",
                    "eCStrip_PS", "eCAudioEmitter_PS", "eCDecal_PS", "eCBillboard_PS", "eCMesh_PS"].contains(c)).collect::<Vec<_>>().join("+");
                *classes.entry(class).or_default() += 1;
            }
        }
    }
    println!("{} templates: old resource-only {}, new template {}, instance property-sets {}; improved {}, changed old matches {}; mismatches {classes:?}",
        seen.len(), counts[0], counts[1], counts[2], counts[3], counts[4]);
    Ok(())
}
