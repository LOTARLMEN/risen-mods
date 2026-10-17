//! Stage a separate four-object custom mesh scene; never writes to the game.
use risen_formats::{bin_io::{Reader, Writer}, collider::{self, Surface, VerticalSurface}, collision,
    geometry::Mesh, lrent::{Lrent, IDENTITY}, props::{ClassData, PropertyObject},
    tple::Tple, vfs::GameVfs, xcom::CollisionMesh};
use std::{collections::HashMap, io::Write, path::PathBuf, sync::Arc};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

fn main() -> Result<()> {
    let root = PathBuf::from(std::env::args_os().nth(1).ok_or("game root")?).canonicalize()?;
    let stage = PathBuf::from(std::env::args_os().nth(2).ok_or("stage")?).canonicalize()?;
    let allowed = root.join("risen-mods/build/tests").canonicalize()?;
    if stage == allowed || !stage.starts_with(&allowed) { return Err("stage must be inside build/tests".into()); }
    let vfs = GameVfs::open(&root)?;
    let layer_path = stage.join("world/data/common/projects/World/RisenMods/RisenMods_Assets_Test.lrent");
    let mut layer = Lrent::read(&std::fs::read(layer_path)?)?;
    if layer.entities().len() != 1 { return Err("expected new empty staged layer".into()); }
    let old = Lrent::read(&vfs.read(vfs.find("data/common/projects", "RisenMods_NewLayer_Test.lrent")?)?)?;
    let terrain = Lrent::read(&vfs.read(vfs.find("data/common/projects", "Levelmesh_Landscape_COL.lrent")?)?)?;
    let template = Tple::read(&vfs.read(vfs.find("data/common/templates", "Obj_Furn_Plank_Board_01.tple")?)?)?;
    let mut cache = HashMap::<String, Arc<CollisionMesh>>::new();
    let mut placed: Vec<[f32;6]> = Vec::new();
    let mut layout = String::from("index,name,guid,x,y,z,terrain_min_cm,terrain_max_cm\n");
    for suffix in ["Ramp", "Stretch", "Trim", "Terrain"] {
        let name = format!("RisenMods_Probe_{suffix}");
        let mesh = Mesh::read_xmsh(&std::fs::read(stage.join(format!("{name}._xmsh")))?)?;
        let bounds = mesh.bounds().ok_or("empty custom mesh")?;
        let physics = Arc::new(CollisionMesh::read(&std::fs::read(stage.join(format!("{name}._xcom")))?)?);
        let mut selected = None;
        'sites: for z in [-6200., -6600., -5800., -5400., -5000., -4600.] {
            for x in [-37400., -37000., -36600., -36200., -37800., -38200., -38600., -35800.] {
                let region = [x+bounds[0]-30., -500., z+bounds[2]-30., x+bounds[3]+30., 2000., z+bounds[5]+30.];
                let overlaps = |b: &[f32;6]| b[0] <= region[3] && b[3] >= region[0] && b[2] <= region[5] && b[5] >= region[2];
                if old.entities().iter().skip(1).any(|e| overlaps(&e.world_box)) || placed.iter().any(overlaps) { continue; }
                let mut surfaces = Vec::new();
                for e in terrain.entities().iter().skip(1).filter(|e| e.enabled != 0 && e.collision_enabled != 0) {
                    let objects: Vec<_> = e.property_sets.iter().map(|s| &s.object).collect();
                    surfaces.extend(collider::surfaces(&objects, terrain.strings(), &e.world_matrix, true, Some(region), |name| {
                        if let Some(c) = cache.get(name) { return Ok(c.clone()); }
                        let c = Arc::new(CollisionMesh::read(&vfs.read(vfs.find("data/common/physics", name).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?);
                        cache.insert(name.to_owned(), c.clone()); Ok(c)
                    })?);
                }
                for s in &mut surfaces { if let Surface::Mesh(m) = s {
                    let vertices = &m.vertices;
                    m.triangles.retain(|f| [0,2].into_iter().all(|k| {
                        f.iter().map(|&i| vertices[i as usize][k]).fold(f32::INFINITY,f32::min) <= region[k+3] &&
                        f.iter().map(|&i| vertices[i as usize][k]).fold(f32::NEG_INFINITY,f32::max) >= region[k]
                    }));
                }}
                let mut heights = Vec::new();
                for iz in 0..5 { for ix in 0..5 {
                    let px = x+bounds[0]+(bounds[3]-bounds[0])*ix as f32/4.;
                    let pz = z+bounds[2]+(bounds[5]-bounds[2])*iz as f32/4.;
                    if let Some(y) = surfaces.iter().filter_map(|s| s.surface_y(px,pz,-500.,2000.)).max_by(f32::total_cmp) { heights.push(y); }
                }}
                if heights.len() != 25 { continue; }
                let low = heights.iter().copied().fold(f32::INFINITY, f32::min);
                let high = heights.iter().copied().fold(f32::NEG_INFINITY, f32::max);
                if low < 8. || high > 250. || high-low > 50. { continue; }
                // Raise all four meshes clear of terrain; 20cm edges can be stepped/jumped onto.
                selected = Some(([x, high+20.-bounds[1], z], low, high));
                placed.push(region); break 'sites;
            }
        }
        let (position, low, high) = selected.ok_or("no unoccupied dry land site found")?;
        let i = layer.spawn_template(&template, position, bounds)?;
        let model = layer.archive.intern(&format!("{name}.xmsh"));
        let collision_name = layer.archive.intern(&format!("{name}.xcom"));
        let entity_name = layer.archive.intern(&name);
        let strings = layer.strings().to_vec();
        let e = &mut layer.context_mut().unwrap().entities[i]; e.name = entity_name;
        if e.property_sets.iter().any(|s|s.object.class(&strings)=="eCRigidBody_PS") { return Err("expected static donor".into()); }
        for ps in &mut e.property_sets {
            match ps.object.class(&strings) {
                "eCMesh_PS" => {
                    let p = ps.object.body.as_mut().unwrap().props.list.iter_mut().find(|p|strings[p.name.0 as usize]=="MeshFileName").ok_or("mesh filename missing")?;
                    if p.value.len()!=2 { return Err("unknown mesh string layout".into()); } p.value = model.0.to_le_bytes().to_vec();
                }
                "eCCollisionShape_PS" => {
                    let body = ps.object.body.as_mut().unwrap();
                    let ClassData::Raw(raw) = &body.data else { return Err("expected collision class data".into()); };
                    let mut r = Reader::new(raw); let version = r.u16()?;
                    if r.u32()? != 1 { return Err("expected exactly one box shape".into()); }
                    let mut shape = PropertyObject::read(&mut r, &strings)?;
                    let tail = r.bytes(r.remaining())?.to_vec();
                    let sb = shape.body.as_mut().ok_or("empty collision shape")?;
                    let ClassData::Raw(shape_raw) = &sb.data else { return Err("expected raw box shape".into()); };
                    if shape_raw.len()!=100 || u16::from_le_bytes(shape_raw[..2].try_into()?)!=74 { return Err("expected native v74 box".into()); }
                    let mut out = Writer::default(); out.u16(74); collision_name.write(&mut out); out.u16(0);
                    out.bytes(&shape_raw[62..64]); out.f32s(&bounds); out.bytes(&shape_raw[88..100]);
                    sb.data = ClassData::Raw(out.buf);
                    let kind = sb.props.list.iter_mut().find(|p|strings[p.name.0 as usize]=="ShapeType").ok_or("shape kind missing")?;
                    if kind.value.len()!=6 { return Err("unknown shape enum layout".into()); }
                    kind.value[2..].copy_from_slice(&1u32.to_le_bytes());
                    let mut raw = Writer::default(); raw.u16(version); raw.u32(1); shape.write(&mut raw); raw.bytes(&tail);
                    body.data = ClassData::Raw(raw.buf);
                    collision::set_climbable(&mut ps.object, &strings, true)?;
                }
                _ => {}
            }
        }
        let mut pose = IDENTITY; pose[12..15].copy_from_slice(&position); layer.set_world_matrix(i, pose)?;
        let e = &layer.entities()[i];
        let objects: Vec<_> = e.property_sets.iter().map(|p|&p.object).collect();
        let surfaces = collider::surfaces(&objects, layer.strings(), &e.world_matrix, false, None, |_|Ok(physics.clone()))?;
        if surfaces.len()!=1 || !matches!(surfaces[0], Surface::Mesh(_)) { return Err("custom triangle collider missing".into()); }
        let world = mesh.transformed(&e.world_matrix)?;
        let actual = surfaces[0].surface_y(position[0],position[2]+(bounds[2]+bounds[5])/2.,-500.,2000.).ok_or("no custom physical top")?;
        let expected = world.surface_y(position[0],position[2]+(bounds[2]+bounds[5])/2.,-500.,2000.).ok_or("no custom visual top")?;
        if (actual-expected).abs()>0.01 { return Err("world visual/physical mismatch".into()); }
        layout.push_str(&format!("{i},{name},{},{},{},{},{low},{high}\n",e.id.text(),position[0],position[1],position[2]));
        println!("{name} at {position:?}, terrain {low}..{high}");
    }
    let bytes = layer.write(); if Lrent::read(&bytes)?.write()!=bytes {return Err("scene round trip mismatch".into());}
    for (name, data) in [("scene.lrent", bytes), ("scene-layout.csv", layout.into_bytes())] {
        std::fs::OpenOptions::new().write(true).create_new(true).open(stage.join(name))?.write_all(&data)?;
    }
    Ok(())
}
