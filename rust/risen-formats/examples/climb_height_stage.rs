//! Stage a height ladder using the existing readers/placement tools; never writes game data.
use risen_formats::{bounds::string_value, collision::{boolean, set_climbable, shapes}, geometry::Mesh,
    lrent::Lrent, placement::footprint, resource::compiled_name, tple::Tple, vfs::GameVfs};
use std::{fs::OpenOptions, io::Write, path::PathBuf};
use std::fmt::Write as _;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let game = PathBuf::from(std::env::args().nth(1).ok_or("game path required")?).canonicalize()?;
    let stage = PathBuf::from(std::env::args().nth(2).ok_or("existing build/tests subdirectory required")?).canonicalize()?;
    let allowed = game.join("risen-mods/build/tests").canonicalize()?;
    if stage == allowed || !stage.starts_with(&allowed) { return Err("staging must be inside risen-mods/build/tests".into()); }
    let vfs = GameVfs::open(&game)?;
    let original_bytes = vfs.read(vfs.find("data/common/projects", "RisenMods_NewLayer_Test.lrent")?)?;
    let original = Lrent::read(&original_bytes)?;
    if original.entities().len() != 15 { return Err("expected the confirmed 15-entity climbing setup".into()); }
    let terrain_layer = Lrent::read(&vfs.read(vfs.find("data/common/projects", "Levelmesh_Landscape.lrent")?)?)?;
    let terrain_entity = &terrain_layer.entities()[1];
    let ps = terrain_entity.property_set(terrain_layer.strings(), "eCMesh_PS").ok_or("no terrain mesh PS")?;
    let resource = string_value(ps, terrain_layer.strings(), "MeshFileName").ok_or("no terrain mesh name")?;
    let terrain = Mesh::read_xmsh(&vfs.read(vfs.find("data/common/meshes", &compiled_name(resource, "_xmsh"))?)?)?
        .transformed(&terrain_entity.world_matrix)?;
    let t = Tple::read(&vfs.read(vfs.find("data/common/templates", "Obj_Dec_Crate_01.tple")?)?)?;
    let te = t.template().ok_or("multi-template crate")?;
    let mesh_ps = te.sets.iter().find(|s| s.object.class(t.strings()) == "eCMesh_PS").ok_or("no crate mesh PS")?;
    let resource = string_value(&mesh_ps.object, t.strings(), "MeshFileName").ok_or("no crate mesh name")?;
    let mesh = Mesh::read_xmsh(&vfs.read(vfs.find("data/common/meshes", &compiled_name(resource, "_xmsh"))?)?)?;
    let bounds = mesh.bounds().ok_or("empty crate mesh")?;
    let height = bounds[4] - bounds[1];
    let mut layer = original.clone();
    let mut rows = String::from("boxes,first,last,x,z,base_y,top_y,front_ground_y,front_height,back_ground_y,back_height\n");
    let mut ranges = Vec::new();
    for (slot, count) in [1usize, 2, 4, 5, 6].into_iter().enumerate() {
        let x = -37850.0 + (slot % 3) as f32 * 400.0;
        let z = -4900.0 + (slot / 3) as f32 * 400.0;
        let surface = |x, z| terrain.surface_y(x, z, -1000., 2000.).ok_or_else(|| format!("no terrain at {x},{z}"));
        let footprint = footprint([x + bounds[0], bounds[1], z + bounds[2], x + bounds[3], bounds[4], z + bounds[5]])?;
        let mut ground = f32::NEG_INFINITY;
        for [px, pz] in footprint { ground = ground.max(surface(px, pz)?); }
        let front = surface(x, z + bounds[5] + 80.)?;
        let back = surface(x, z + bounds[2] - 80.)?;
        // Keep the walk-up samples on land; underwater terrain cannot be used for this check.
        if ground < 0. || front < 0. || back < 0. { return Err(format!("tower {count}: shoreline/underwater site; choose another layout").into()); }
        let base_y = ground - 2. - bounds[1];
        let first = layer.entities().len();
        for level in 0..count {
            let i = layer.spawn_template(&t, [x, base_y + height * level as f32, z], bounds)?;
            layer.set_yaw(i, 0.)?;
            let strings = layer.strings().to_vec();
            let entity = &mut layer.context_mut().unwrap().entities[i];
            let ps: Vec<_> = entity.property_sets.iter_mut().filter(|s| s.object.class(&strings) == "eCCollisionShape_PS").collect();
            if ps.len() != 1 { return Err("expected one crate collision PS".into()); }
            set_climbable(&mut ps.into_iter().next().unwrap().object, &strings, true)?;
            let entity = &layer.entities()[i];
            let collider = entity.property_set(layer.strings(), "eCCollisionShape_PS").unwrap();
            assert_eq!(boolean(collider, layer.strings(), "IsClimbable"), Some(true));
            assert_eq!(shapes(collider, layer.strings())?.len(), 1);
            assert_eq!(shapes(collider, layer.strings())?[0].kind, 3);
            assert!(entity.property_set(layer.strings(), "eCRigidBody_PS").is_none());
            assert_eq!(entity.collision_enabled, 1);
            if level > 0 {
                assert!((entity.world_box[1] - layer.entities()[i - 1].world_box[4]).abs() < 0.02);
            }
        }
        let last = layer.entities().len() - 1;
        let top = layer.entities()[last].world_box[4];
        writeln!(rows, "{count},{first},{last},{x},{z},{base_y},{top},{front},{},{back},{}", top - front, top - back)?;
        println!("{count} boxes [{first}..{last}] at x={x}, z={z}; visual walk-up height front {:.1} / back {:.1} cm", top - front, top - back);
        ranges.push((first, last));
    }
    // Restore just the appended entities/root aggregates in a clone; all pre-existing bytes
    // must match, including existing entity transforms/properties, archive headers and strings.
    assert_eq!(layer.strings(), original.strings());
    let mut masked = layer.clone();
    let original_context = original.context().unwrap();
    let context = masked.context_mut().unwrap();
    context.entities.truncate(original_context.entities.len());
    context.entities[0] = original_context.entities[0].clone();
    context.hierarchy = original_context.hierarchy.clone();
    context.context_box = original_context.context_box;
    assert_eq!(masked.write(), original_bytes, "unexpected changes outside appended towers/context bounds");
    let bytes = layer.write();
    let readback = Lrent::read(&bytes)?;
    assert_eq!(readback.write(), bytes);
    assert_eq!(readback.entities().len(), 33);
    for (first, last) in ranges {
        for i in first..=last {
            assert_eq!(readback.entities()[i].world_matrix, layer.entities()[i].world_matrix);
            assert_eq!(readback.context().unwrap().parent(i), Some(0));
        }
    }
    OpenOptions::new().write(true).create_new(true).open(stage.join("climb-heights.lrent"))?.write_all(&bytes)?;
    OpenOptions::new().write(true).create_new(true).open(stage.join("heights.csv"))?.write_all(rows.as_bytes())?;
    assert_eq!(vfs.read(vfs.find("data/common/projects", "RisenMods_NewLayer_Test.lrent")?)?, original_bytes);
    println!("STAGED: 18 new climbable crates, 33 total entities; old entity bytes, contacts and readback checked. Visual terrain samples only; runtime mode is selected by the game.");
    Ok(())
}
