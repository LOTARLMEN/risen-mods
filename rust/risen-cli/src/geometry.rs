use super::*;
use risen_formats::bounds::{enum_value, string_value};
use risen_formats::collision::{boolean, shapes, shape_name};
use risen_formats::geometry::{Mesh, actor_nodes, actor_meshes};
use risen_formats::lrent::{IDENTITY, transform_vector};
use risen_formats::xcom::{CollisionMesh, Kind};
use risen_formats::collider::{self,Surface};
use serde_json::{Value, json};

struct CollisionCache<'a> {
    index:std::collections::HashMap<String,Vec<&'a risen_formats::vfs::VfsEntry>>,
    meshes:std::collections::HashMap<String,std::sync::Arc<CollisionMesh>>,
}
impl<'a> CollisionCache<'a> {
    fn new(vfs:&'a GameVfs)->Self {
        let mut index=std::collections::HashMap::<String,Vec<_>>::new();
        for e in vfs.files("data/common/physics","._xcom") {
            index.entry(e.path.rsplit('/').next().unwrap().to_ascii_lowercase()).or_default().push(e);
        }
        Self{index,meshes:std::collections::HashMap::new()}
    }
}
fn collision_surfaces(vfs:&GameVfs,cache:&mut CollisionCache<'_>,objects:&[&PropertyObject],strings:&[String],
    matrix:&[f32;16],support:bool,region:Option<[f32;6]>)->Result<Vec<Surface>,Box<dyn std::error::Error>> {
    Ok(collider::surfaces(objects,strings,matrix,support,region,|name| {
        let key=name.to_ascii_lowercase();
        if let Some(c)=cache.meshes.get(&key){return Ok(c.clone());}
        let entries=cache.index.get(&key).ok_or_else(||format!("missing collision resource {name}"))?;
        if entries.len()!=1 {return Err(format!("ambiguous collision resource {name}"));}
        let entry=entries[0];
        let c=std::sync::Arc::new(CollisionMesh::read(&vfs.read(entry).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?);
        cache.meshes.insert(key,c.clone());Ok(c)
    })?)
}

fn entity_bounds(vfs: &GameVfs, layer: &Lrent, i: usize) -> Result<[f32; 6], Box<dyn std::error::Error>> {
    let e = &layer.entities()[i];
    let objects: Vec<_> = e.property_sets.iter().map(|s| &s.object).collect();
    match static_mesh(vfs, &objects, layer.strings())? {
        Some((_, mesh)) => mesh.transformed(&e.world_matrix)?.bounds().ok_or_else(|| "empty render mesh".into()),
        None => Err("placement currently requires a static render mesh; animated/procedural objects need a geometry provider".into()),
    }
}

/// Placement installs get the same verified snapshots/rollback as layer creation. Explicit scratch
/// outputs are ordinary files; writes to game project paths always go through project::apply.
fn save_placement(vfs: &GameVfs, layer: &EditLayer, out: Option<PathBuf>) -> Result<(), Box<dyn std::error::Error>> {
    if out.is_none() && !layer.default_writable { return Err("NoPhysical=true: use --out or enable loose files".into()); }
    let dest = out.clone().unwrap_or_else(|| layer.loose.clone());
    let absolute = std::path::absolute(&dest)?;
    let mut ancestor = absolute.as_path();
    let mut tail = Vec::new();
    while !ancestor.exists() {
        tail.push(ancestor.file_name().ok_or("output has no filename")?.to_os_string());
        ancestor = ancestor.parent().ok_or("output has no existing parent")?;
    }
    let mut resolved = std::fs::canonicalize(ancestor)?;
    for name in tail.iter().rev() { resolved.push(name); }
    let root = std::fs::canonicalize(&vfs.root)?;
    if resolved.starts_with(&root) && !resolved.starts_with(root.join("risen-mods")) {
        let path = resolved.strip_prefix(&root)?.to_string_lossy().replace('\\', "/");
        let bytes = layer.lrent.write(); Lrent::read(&bytes)?;
        let backup = project::apply(&root, &[project::Change { path, bytes, replace: true }])?;
        println!("BACKUP: {}", backup.display());
        println!("RESTORE: risen world-restore \"{}\"", backup.display());
    } else { layer.save(out)?; }
    Ok(())
}

pub(super) fn physics(vfs: &GameVfs, source: &str, spec: &str, mode: &str, out: Option<PathBuf>, dry: bool)
    -> Result<(), Box<dyn std::error::Error>>
{
    use risen_formats::physics::{Mode, set_mode};
    let mode = match mode { "dynamic" => Mode::Dynamic, "frozen" => Mode::Frozen, _ => return Err("invalid physics mode".into()) };
    let mut layer = open_layer_for_edit(vfs, source)?;
    let i = layer.entity_index(spec)?;
    let strings = layer.lrent.strings().to_vec();
    let e = &mut layer.lrent.context_mut().ok_or("missing entity context")?.entities[i];
    let indices: Vec<_> = e.property_sets.iter().enumerate().filter(|(_, s)| s.object.class(&strings) == "eCRigidBody_PS").map(|(j, _)| j).collect();
    if indices.len() != 1 { return Err("physics requires exactly one existing eCRigidBody_PS; static objects are not converted".into()); }
    let (old, new) = set_mode(&mut e.property_sets[indices[0]].object, &strings, mode)?;
    println!("physics {}: {mode:?}, BodyFlag {old} -> {new}; physics enabled; initial wake counter {}",
        layer.describe(i), if matches!(mode, Mode::Dynamic) { 1 } else { 0 });
    println!("selected entity only; runtime scripts may override motion flags");
    if !dry { save_placement(vfs, &layer, out)?; }
    Ok(())
}

pub(super) fn climbable(vfs: &GameVfs, source: &str, spec: &str, enabled: bool, out: Option<PathBuf>, dry: bool)
    -> Result<(), Box<dyn std::error::Error>>
{
    let mut layer = open_layer_for_edit(vfs, source)?;
    let i = layer.entity_index(spec)?;
    let strings = layer.lrent.strings().to_vec();
    let e = &mut layer.lrent.context_mut().ok_or("missing entity context")?.entities[i];
    let indices: Vec<_> = e.property_sets.iter().enumerate().filter(|(_, s)|
        s.object.class(&strings) == "eCCollisionShape_PS").map(|(j, _)| j).collect();
    if indices.len() != 1 { return Err("climbable requires exactly one existing eCCollisionShape_PS".into()); }
    let old = risen_formats::collision::set_climbable(&mut e.property_sets[indices[0]].object, &strings, enabled)?;
    println!("climbable {}: IsClimbable {old} -> {enabled}", layer.describe(i));
    println!("selected entity only; ledge geometry, clearance and native owner policy still apply");
    if !dry { save_placement(vfs, &layer, out)?; }
    Ok(())
}

pub(super) fn snap(vfs: &GameVfs, source: &str, spec: &str, against: &str, target_layer: Option<&str>, side: &str,
    gap: f32, center: bool, out: Option<PathBuf>, dry: bool) -> Result<(), Box<dyn std::error::Error>>
{
    use risen_formats::placement::{Side, snap};
    let mut layer = open_layer_for_edit(vfs, source)?;
    let i = layer.entity_index(spec)?;
    let target = open_layer_for_edit(vfs, target_layer.unwrap_or(source))?;
    let j = target.entity_index(against)?;
    let tree = layer.lrent.context().ok_or("missing entity context")?.checked_subtree(i)?;
    if tree.iter().any(|&k| layer.lrent.entities()[k].id == target.lrent.entities()[j].id) {
        return Err("snap target belongs to the moving subtree".into());
    }
    let side = match side { "top" => Side::Top, "bottom" => Side::Bottom, "left" => Side::Left, "right" => Side::Right,
        "front" => Side::Front, "back" => Side::Back, _ => return Err("invalid snap side".into()) };
    let a = entity_bounds(vfs, &layer.lrent, i)?;
    let b = entity_bounds(vfs, &target.lrent, j)?;
    let d = snap(a, b, side, gap, center)?;
    println!("snap {} against {} using world render AABB faces; offset {d:?}, gap {gap} cm", layer.describe(i), target.describe(j));
    layer.lrent.translate(i, d)?;
    println!("  -> {:?}", layer.lrent.entities()[i].position());
    if !dry { save_placement(vfs, &layer, out)?; }
    Ok(())
}

pub(super) fn ground(vfs: &GameVfs, source: &str, spec: &str, collision_mode:bool, height: Option<f32>, align: bool, samples: Option<usize>, max_slope: f32, surface_layer: Option<&str>,
    world: &str, drop: f32, rise: f32, gap: f32, out: Option<PathBuf>, dry: bool) -> Result<(), Box<dyn std::error::Error>>
{
    if ![drop, rise, gap].iter().all(|v| v.is_finite()) || drop < 0. || rise < 0. || height.is_some_and(|y| !y.is_finite()) {
        return Err("placement distances must be finite; drop/rise must be nonnegative".into());
    }
    let mut layer = open_layer_for_edit(vfs, source)?;
    let i = layer.entity_index(spec)?;
    let mut cache=CollisionCache::new(vfs);
    let dense = collision_mode || align || samples.is_some();
    let world_matrix = layer.lrent.entities()[i].world_matrix;
    let dense_mesh = if dense {
        let e = &layer.lrent.entities()[i];
        let objects: Vec<_> = e.property_sets.iter().map(|s| &s.object).collect();
        if collision_mode {
            if e.enabled==0 || e.collision_enabled==0 {return Err("moving entity collision is disabled".into());}
            // Local geometry is sampled by the existing dense placement algorithm; world pose
            // validation is also performed for native scale compatibility.
            collision_surfaces(vfs,&mut cache,&objects,layer.lrent.strings(),&world_matrix,false,None)?;
            Some(collider::merge_meshes(collision_surfaces(vfs,&mut cache,&objects,layer.lrent.strings(),&IDENTITY,false,None)?)?)
        } else {Some(static_mesh(vfs, &objects, layer.lrent.strings())?.ok_or("missing static placement mesh")?.1)}
    } else { None };
    let bounds=if collision_mode {dense_mesh.as_ref().unwrap().transformed(&world_matrix)?.bounds().ok_or("empty moving collider")?}
        else {entity_bounds(vfs,&layer.lrent,i)?};
    let geometry_label=if collision_mode {"collision"}else{"visual"};
    let mut search_bounds = bounds;
    if align {
        // A tilted tall or offset model can extend beyond its original XZ bounds. Use a sphere
        // around the entity origin to conservatively include all candidate surfaces after rotation.
        let radius = dense_mesh.as_ref().unwrap().vertices.iter().map(|&p| {
            let p = transform_vector(p, &world_matrix);
            p[0].hypot(p[1]).hypot(p[2])
        }).max_by(f32::total_cmp).ok_or("empty mesh")?;
        for k in [0,2] { search_bounds[k]=world_matrix[12+k]-radius; search_bounds[3+k]=world_matrix[12+k]+radius; }
        if !search_bounds.iter().all(|v|v.is_finite()) { return Err("ground search bounds overflow".into()); }
    }
    let moving: std::collections::HashSet<_> = layer.lrent.context().ok_or("missing context")?.checked_subtree(i)?.iter()
        .map(|&j| layer.lrent.entities()[j].id.guid).collect();
    let mut surfaces = Vec::new();
    let mut names = Vec::new();
    let mut skipped = Vec::new();
    let d = if let Some(y) = height { y + gap - bounds[1] } else {
        let mut scan = |l: &Lrent, label: &str, surfaces: &mut Vec<Surface>, names: &mut Vec<String>, skipped: &mut Vec<String>| {
            for (j, e) in l.entities().iter().enumerate().skip(1) {
                if moving.contains(&e.id.guid) {continue;}
                if collision_mode {
                    if e.enabled==0 || e.collision_enabled==0 {continue;}
                    let objects:Vec<_>=e.property_sets.iter().map(|s|&s.object).collect();
                    if objects.iter().any(|o|o.class(l.strings())=="eCRigidBody_PS"){continue;}
                    let mut region=search_bounds;region[1]=bounds[1]-drop;region[4]=bounds[1]+rise;
                    match collision_surfaces(vfs,&mut cache,&objects,l.strings(),&e.world_matrix,true,Some(region)) {
                        Ok(found)=>for (shape,mut surface) in found.into_iter().enumerate() {
                            let Some(b)=surface.bounds() else {continue;};
                            if b[0]>search_bounds[3] || b[3]<search_bounds[0] || b[2]>search_bounds[5] || b[5]<search_bounds[2]
                                || b[1]>bounds[1]+rise || b[4]<bounds[1]-drop {continue;}
                            if let Surface::Mesh(mesh)=&mut surface {clip_surface(mesh,search_bounds);}
                            surfaces.push(surface);names.push(format!("{label} [{j}] collider {shape}"));
                        },
                        Err(err)=>skipped.push(format!("{label} [{j}]: {err}")),
                    }
                    continue;
                }
                if e.rendering_enabled==0 {continue;}
                let b = e.world_box;
                if b[0] > search_bounds[3] || b[3] < search_bounds[0] || b[2] > search_bounds[5] || b[5] < search_bounds[2]
                    || b[1] > bounds[1] + rise || b[4] < bounds[1] - drop { continue; }
                let s = l.strings();
                let objects: Vec<_> = e.property_sets.iter().map(|s| &s.object).collect();
                if objects.iter().any(|o| o.class(s) == "eCRigidBody_PS") { continue; }
                let collision = objects.iter().find(|o| o.class(s) == "eCCollisionShape_PS");
                if collision.is_some_and(|o| boolean(o, s, "DisableCollision") == Some(true) || boolean(o, s, "DisableResponse") == Some(true)) { continue; }
                match static_mesh(vfs, &objects, s).and_then(|m| m.map(|(p, m)| m.transformed(&e.world_matrix).map(|m| (p, m)).map_err(Into::into)).transpose()) {
                    Ok(Some((path, mut mesh))) => {
                        if dense {
                            let vertices=&mesh.vertices;
                            mesh.triangles.retain(|t| [0,2].into_iter().all(|k| {
                                let min=t.iter().map(|&i|vertices[i as usize][k]).min_by(f32::total_cmp).unwrap();
                                let max=t.iter().map(|&i|vertices[i as usize][k]).max_by(f32::total_cmp).unwrap();
                                min<=search_bounds[3+k] && max>=search_bounds[k]
                            }));
                        }
                        surfaces.push(Surface::Mesh(mesh)); names.push(format!("{label} [{j}] {path}"));
                    }
                    Ok(None) => {}
                    Err(err) => skipped.push(format!("{label} [{j}]: {err}")),
                }
            }
        };
        if let Some(spec) = surface_layer {
            let target = open_layer_for_edit(vfs, spec)?;
            scan(&target.lrent, &target.src, &mut surfaces, &mut names, &mut skipped);
        } else {
            // Use the current in-memory layer and skip its older mounted copy.
            scan(&layer.lrent, source, &mut surfaces, &mut names, &mut skipped);
            let active = active_layers(vfs, world)?;
            for e in vfs.files(PROJECTS, ".lrent").filter(|e| active.contains(&e.path.to_ascii_lowercase())) {
                if same_file(&e.loose, &layer.loose) { continue; }
                let l = Lrent::read(&vfs.read(e)?)?;
                if l.entities().first().map(|e| e.id) == layer.lrent.entities().first().map(|e| e.id) { continue; }
                scan(&l, &e.path, &mut surfaces, &mut names, &mut skipped);
            }
        }
        println!("{geometry_label}-surface search: {} static candidates, {} unavailable", surfaces.len(), skipped.len());
        for name in &names { println!("  surface candidate: {name}"); }
        for s in &skipped { eprintln!("unavailable surface: {s}"); }
        // Incomplete candidates could hide a higher support. Require an explicit, complete surface
        // layer or a plane instead of silently settling through an unreadable mesh.
        if !skipped.is_empty() { return Err("surface search is incomplete; use --surface-layer or --height".into()); }
        if dense { 0. } else {
            let d = risen_formats::placement::surface_offset_on(bounds, &surfaces, drop, rise, gap)?;
            println!("support sampled at center + four world-AABB corners (render geometry; PhysX contact may differ)");
            d
        }
    };
    if dense {
        let plan = risen_formats::placement::ground_plan_on(dense_mesh.as_ref().unwrap(), world_matrix, &surfaces,
            height, drop, rise, gap, align, samples.unwrap_or(7), max_slope)?;
        println!("ground {}: sampled tilt {:.3} degrees, normal {:?}, {} model/grid samples, gap {gap} cm",
            layer.describe(i), plan.slope_degrees, plan.normal, plan.samples);
        println!("vertical offset {} cm; largest sampled underside clearance above requested gap {:.3} cm ({geometry_label} geometry)",
            plan.world_matrix[13]-world_matrix[13], plan.max_clearance);
        layer.lrent.set_world_matrix(i, plan.world_matrix)?;
    } else {
        if !d.is_finite() { return Err("ground offset overflows".into()); }
        println!("ground {}: vertical offset {d} cm, gap {gap} cm", layer.describe(i));
        layer.lrent.translate(i, [0., d, 0.])?;
    }
    println!("  -> {:?}", layer.lrent.entities()[i].position());
    if !dry { save_placement(vfs, &layer, out)?; }
    Ok(())
}

fn clip_surface(mesh:&mut Mesh,query:[f32;6]) {
    let vertices=&mesh.vertices;
    mesh.triangles.retain(|t|[0,2].into_iter().all(|k| {
        let min=t.iter().map(|&i|vertices[i as usize][k]).min_by(f32::total_cmp).unwrap();
        let max=t.iter().map(|&i|vertices[i as usize][k]).max_by(f32::total_cmp).unwrap();
        min<=query[3+k] && max>=query[k]
    }));
}

/// Initial world membership, not merely every mounted project (which also includes test/import worlds).
fn active_layers(vfs: &GameVfs, world: &str) -> Result<std::collections::HashSet<String>, Box<dyn std::error::Error>> {
    let name = if world.to_ascii_lowercase().ends_with(".wrl") { world.to_string() } else { format!("{world}.wrl") };
    let w = ProjectFile::read(&vfs.read(vfs.find(PROJECTS, &name)?)?)?;
    let index = w.world().and_then(|w| w.world_data_file).ok_or("world has no sector list")?;
    let entry = vfs.find(PROJECTS, w.archive.str(index))?;
    let data = WorldData::read(&vfs.read(entry)?)?;
    let local_name = format!("{}_local.wrldatasc", entry.path.strip_suffix(".wrldatasc").ok_or("invalid sector-list path")?);
    let local = match vfs.find(PROJECTS, &local_name) {
        Ok(e) => Some(WorldData::read(&vfs.read(e)?)?),
        Err(VfsError::NotFound(_)) => None,
        Err(e) => return Err(e.into()),
    };
    let mut active = std::collections::HashSet::new();
    for e in data.sectors().iter().filter(|e| e.enabled) {
        if local.as_ref().is_some_and(|l| !l.sectors().iter().any(|s| s.name.eq_ignore_ascii_case(&e.name) && s.enabled)) { continue; }
        let sector = ProjectFile::read(&vfs.read(vfs.find(PROJECTS, &format!("{}.sec", e.name))?)?)?;
        let s = sector.sector().ok_or("sector reference is not a sector")?;
        if s.enabled == Some(0) { continue; }
        for &name in &s.layers {
            let layer = vfs.find(PROJECTS, &format!("{}.lrent", sector.archive.str(name)))?;
            active.insert(layer.path.to_ascii_lowercase());
        }
    }
    Ok(active)
}

pub(super) fn render_refs(objects: &[&PropertyObject], strings: &[String]) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for o in objects {
        let (prop, scope, ext) = match o.class(strings) {
            "eCMesh_PS" => ("MeshFileName", "data/common/meshes", "_xmsh"),
            "eCAnimation_PS" | "eCBodyPart_PS" => ("ResourceFilePath", "data/compiled/animations", "_xmac"),
            "eCSpeedTree_PS" => ("ResourceFilePath", "data/compiled/speedtrees", "_xspt"),
            _ => continue,
        };
        if let Some(file) = string_value(o, strings, prop) {
            out.push((scope.to_string(), risen_formats::resource::compiled_name(file, ext)));
        }
    }
    out
}

pub(super) fn static_mesh(vfs: &GameVfs, objects: &[&PropertyObject], strings: &[String]) -> Result<Option<(String, Mesh)>, Box<dyn std::error::Error>> {
    for (scope, name) in render_refs(objects, strings) {
        if !name.ends_with("._xmsh") { continue; }
        let e = vfs.find(&scope, &name)?;
        return Ok(Some((e.path.clone(), Mesh::read_xmsh(&vfs.read(e)?)?)));
    }
    Ok(None)
}

fn resource_info(path: &str, bytes: &[u8]) -> Value {
    let mut info = json!({ "path": path, "resource_bounds": risen_formats::resource::boundary(bytes), "units": "cm", "up_axis": "Y" });
    match bytes.get(..8) {
        Some(b"GR01MS02") => match Mesh::read_xmsh(bytes) {
            Ok(m) => {
                info["kind"] = json!("static_render_mesh");
                info["vertices"] = json!(m.vertices.len()); info["triangles"] = json!(m.triangles.len());
                info["vertex_bounds"] = json!(m.bounds());
            }
            Err(e) => { info["error"] = json!(e.to_string()); }
        },
        Some(b"GR01MA02") => match actor_nodes(bytes) {
            Ok(nodes) => {
                info["kind"] = json!("animated_actor");
                info["nodes"] = json!(nodes.iter().enumerate().map(|(i, n)| json!({ "index": i, "name": n.name,
                    "parent": n.parent, "bind_position": n.position, "bind_rotation": n.rotation })).collect::<Vec<_>>());
                match actor_meshes(bytes) {
                    Ok(meshes) => {
                        info["vertices"] = json!(meshes.iter().map(|(_, m)| m.vertices.len()).sum::<usize>());
                        info["triangles"] = json!(meshes.iter().map(|(_, m)| m.triangles.len()).sum::<usize>());
                        info["mesh_nodes"] = json!(meshes.iter().map(|(n, _)| n).collect::<Vec<_>>());
                        info["pose"] = json!("raw bind-pose vertices; skin deformation not applied");
                    }
                    Err(e) => { info["triangles_error"] = json!(e.to_string()); }
                }
            }
            Err(e) => { info["error"] = json!(e.to_string()); }
        },
        Some(b"GR01CM00") => {
            info["kind"] = json!("cooked_physx_collision");
            match CollisionMesh::read(bytes) {
                Ok(c) => {
                    info["vertices"] = json!(c.meshes.iter().map(|m|m.mesh.vertices.len()).sum::<usize>());
                    info["triangles"] = json!(c.meshes.iter().map(|m|m.mesh.triangles.len()).sum::<usize>());
                    info["meshes"] = json!(c.meshes.iter().map(|m|json!({
                        "kind": if m.kind==Kind::Triangle {"triangle_mesh"} else {"convex_hull"},
                        "resource_index": m.index, "stream_start": m.stream_start, "stream_end": m.stream_end,
                        "vertices": m.mesh.vertices.len(), "triangles": m.mesh.triangles.len(),
                        "vertex_bounds": m.mesh.bounds(), "cooked_bounds": m.bounds,
                        "triangle_materials": m.triangle_materials.as_ref().map(|indices| {
                            let counts=indices.iter().fold(std::collections::BTreeMap::<u16,usize>::new(),|mut counts,&i|{*counts.entry(i).or_default()+=1;counts});
                            counts.into_iter().map(|(index,triangles)|json!({"index":index,"triangles":triangles})).collect::<Vec<_>>()
                        })
                    })).collect::<Vec<_>>());
                    info["materials"] = json!(c.materials.iter().map(|m|json!({"surface":m.surface,
                        "ignored_by_ray":m.ignored_by_ray,"collision_disabled":m.collision_disabled,
                        "response_disabled":m.response_disabled})).collect::<Vec<_>>());
                    info["triangles_status"] = json!("decoded; acceleration trees skipped; no physics simulation");
                }
                Err(e) => { info["error"] = json!(e.to_string()); }
            }
        }
        _ => { info["kind"] = json!("unsupported_resource"); }
    }
    info
}

pub(super) fn inspect(vfs: &GameVfs, source: &str, entity: Option<&str>, output_json: bool, obj: Option<&Path>)
    -> Result<(), Box<dyn std::error::Error>>
{
    let mut mesh_exports = Vec::new();
    let report = if entity.is_none() && ["._xmsh", "._xmac", "._xcom", "._xspt"].iter().any(|e| source.to_lowercase().ends_with(e)) {
        let bytes = if Path::new(source).is_file() { std::fs::read(source)? } else { vfs.read(vfs.find("data", source)?)? };
        if obj.is_some() {
            if bytes.starts_with(b"GR01MA02") {
                for (node, m) in actor_meshes(&bytes)? { mesh_exports.push((format!("{source} node {node}"), m, IDENTITY)); }
            } else if bytes.starts_with(b"GR01CM00") {
                for m in CollisionMesh::read(&bytes)?.meshes {
                    mesh_exports.push((format!("{source} collision {:?} {}",m.kind,m.index), m.mesh, IDENTITY));
                }
            } else { mesh_exports.push((source.to_string(), Mesh::read_xmsh(&bytes)?, IDENTITY)); }
        }
        resource_info(source, &bytes)
    } else {
        let (name, objects, strings, matrix, stored_box, collision_enabled) = if let Some(spec) = entity {
            let layer = open_layer_for_edit(vfs, source)?;
            let i = layer.entity_index(spec)?;
            let e = &layer.lrent.entities()[i];
            (layer.describe(i), e.property_sets.iter().map(|s| s.object.clone()).collect::<Vec<_>>(),
                layer.lrent.strings().to_vec(), e.world_matrix, Some(e.local_box), Some(e.collision_enabled != 0))
        } else {
            let (t, _) = template_source(vfs, source)?;
            let e = t.template().ok_or("multi-template not supported")?;
            (t.name(e).to_string(), e.sets.iter().map(|s| s.object.clone()).collect(), t.strings().to_vec(), IDENTITY,
                None, e.flags.get(4).map(|&v| v != 0))
        };
        let refs: Vec<_> = objects.iter().collect();
        let mut resources = Vec::new();
        for (scope, name) in render_refs(&refs, &strings) {
            match vfs.find(&scope, &name) {
                Ok(e) => {
                    let bytes = vfs.read(e)?;
                    resources.push(resource_info(&e.path, &bytes));
                    if obj.is_some() && name.ends_with("._xmsh") { mesh_exports.push((name, Mesh::read_xmsh(&bytes)?, matrix)); }
                    else if obj.is_some() && name.ends_with("._xmac") {
                        for (node, m) in actor_meshes(&bytes)? { mesh_exports.push((format!("{name} bind-pose node {node}"), m, matrix)); }
                    }
                }
                Err(e) => resources.push(json!({ "name": name, "error": e.to_string() })),
            }
        }
        let rigid = refs.iter().find(|o| o.class(&strings) == "eCRigidBody_PS");
        let mut collisions = Vec::new();
        for o in refs.iter().filter(|o| o.class(&strings) == "eCCollisionShape_PS") {
            let list = shapes(o, &strings)?;
            let mut entries = Vec::new();
            for s in list {
                let resource = s.mesh.as_ref().map(|name| {
                    let compiled = risen_formats::resource::compiled_name(name, "_xcom");
                    match vfs.find("data/common/physics", &compiled) {
                        Ok(e) => match vfs.read(e) {
                            Ok(bytes) => {
                                let mut info=resource_info(&e.path,&bytes);
                                let kind=if s.kind==1 {"triangle_mesh"} else {"convex_hull"};
                                let index=s.resource_index.unwrap_or(0) as usize;
                                info["selected_mesh"]=info["meshes"].as_array().and_then(|m|m.iter().find(|m|
                                    m["kind"]==kind && m["resource_index"]==index)).cloned().unwrap_or(Value::Null);
                                if info["selected_mesh"].is_null() && info.get("error").is_none() {
                                    info["error"]=json!("shape resource index is outside the matching mesh array");
                                }
                                info
                            }
                            Err(err) => json!({"path":e.path,"error":err.to_string()}),
                        },
                        Err(e) => json!({ "name": compiled, "error": e.to_string() }),
                    }
                });
                entries.push(json!({ "kind": shape_name(s.kind), "version": s.version, "resource": resource,
                    "shape_group":s.group,"adapt_to_aabb":s.adapt_to_aabb,"adapt_mode":s.adapt_mode,
                    "resource_index": s.resource_index, "outer_box": s.outer_box, "disabled": s.disabled,
                    "response_disabled": s.response_disabled, "ignored_by_ray": s.ignored_by_ray,
                    "override_entity_aabb": s.override_entity_aabb }));
            }
            collisions.push(json!({ "group": enum_value(o, &strings, "Group"), "disabled": boolean(o, &strings, "DisableCollision"),
                "response_disabled": boolean(o, &strings, "DisableResponse"), "ignored_by_ray": boolean(o, &strings, "IgnoredByTraceRay"),
                "is_climbable": boolean(o, &strings, "IsClimbable"),
                "shapes": entries }));
        }
        let flags = rigid.and_then(|o| enum_value(o, &strings, "BodyFlag"));
        use risen_formats::physics::{FROZEN, KINEMATIC, DISABLE_GRAVITY};
        json!({ "name": name, "units": "cm", "up_axis": "Y", "world_matrix": matrix,
            "stored_local_box": stored_box, "entity_collision_enabled": collision_enabled,
            "rigid_body": rigid.is_some(), "physics_enabled": rigid.and_then(|o| boolean(o, &strings, "PhysicsEnabled")),
            "body_flags": flags,
            "motion": flags.map(|f| json!({ "frozen": f & FROZEN == FROZEN, "locked_axes_mask": f & FROZEN,
                "kinematic": f & KINEMATIC != 0, "gravity_disabled": f & DISABLE_GRAVITY != 0,
                "unknown_flags": f & !(FROZEN | KINEMATIC | DISABLE_GRAVITY) })),
            "wake_up_counter": rigid.and_then(|o| risen_formats::bounds::floats::<1>(o, &strings, "WakeUpCounter")).map(|v| v[0]),
            "resources": resources, "collision_sets": collisions,
            "classes": refs.iter().map(|o| o.class(&strings)).collect::<Vec<_>>() })
    };
    if let Some(path) = obj {
        if mesh_exports.is_empty() { return Err("no decoded triangles to export".into()); }
        if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty()) { std::fs::create_dir_all(parent)?; }
        use std::io::Write;
        // Avoid silently replacing an asset or a previous export.
        let mut f = std::fs::OpenOptions::new().write(true).create_new(true).open(path)?;
        writeln!(f, "# Risen geometry: centimetres, Y up; collision for ._xcom, render for other sources")?;
        let mut base = 1u32;
        for (name, mesh, matrix) in mesh_exports {
            writeln!(f, "# {name}")?;
            writeln!(f, "o mesh_{}",base)?;
            for p in &mesh.vertices {
                let v = transform_vector(*p, &matrix);
                writeln!(f, "v {} {} {}", v[0] + matrix[12], v[1] + matrix[13], v[2] + matrix[14])?;
            }
            for t in mesh.triangles { writeln!(f, "f {} {} {}", t[0] + base, t[1] + base, t[2] + base)?; }
            base += mesh.vertices.len() as u32;
        }
        eprintln!("exported triangles to {}", path.display());
    }
    if output_json { println!("{}", serde_json::to_string_pretty(&report)?); }
    else {
        println!("{}", report.get("name").or(report.get("path")).unwrap_or(&Value::Null));
        if report.get("rigid_body").is_some() {
            println!("rigid body: {}; physics enabled: {}; collision enabled: {}", report["rigid_body"], report["physics_enabled"], report["entity_collision_enabled"]);
            if report["rigid_body"] == true {
                println!("body flags {}; frozen {}; locked axes {}; kinematic {}; gravity disabled {}; wake counter {}",
                    report["body_flags"], report["motion"]["frozen"], report["motion"]["locked_axes_mask"],
                    report["motion"]["kinematic"], report["motion"]["gravity_disabled"], report["wake_up_counter"]);
            }
        }
        for r in report["resources"].as_array().into_iter().flatten().chain(report.get("path").map(|_| &report)) {
            println!("resource {}: {}; vertices {}; triangles {}; actor nodes {}{}", r.get("path").or(r.get("name")).unwrap_or(&Value::Null),
                r["kind"], r["vertices"], r["triangles"], r["nodes"].as_array().map_or(0, Vec::len),
                r.get("error").map_or(String::new(), |e| format!("; {e}")));
        }
        for c in report["collision_sets"].as_array().into_iter().flatten() {
            println!("collision group {}; disabled {}; response disabled {}; climbable {}", c["group"], c["disabled"], c["response_disabled"], c["is_climbable"]);
            for s in c["shapes"].as_array().into_iter().flatten() { println!("  {}: {} (outer box {})", s["kind"], s["resource"], s["outer_box"]); }
        }
    }
    Ok(())
}
