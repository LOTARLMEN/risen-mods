//! Scratch-only native-model comparison scene; placement is subsequently done by the real CLI.
use risen_formats::{bounds::string_value, collider::{self,Surface}, collision::set_climbable,
    geometry::Mesh, lrent::{Lrent,IDENTITY}, resource::compiled_name, tple::Tple,
    vfs::GameVfs, xcom::CollisionMesh};
use std::{collections::HashMap, fs::OpenOptions, io::Write, path::PathBuf, sync::Arc};

fn template(vfs:&GameVfs,name:&str)->Result<(Tple,[f32;6],[f32;6]),Box<dyn std::error::Error>> {
    let t=Tple::read(&vfs.read(vfs.find("data/common/templates",&format!("{name}.tple"))?)?)?;
    let e=t.template().ok_or("expected a single static template")?;
    let objects:Vec<_>=e.sets.iter().map(|s|&s.object).collect();
    if objects.iter().any(|o|o.class(t.strings())=="eCRigidBody_PS") {return Err("expected static template".into());}
    let ps=objects.iter().find(|o|o.class(t.strings())=="eCMesh_PS").ok_or("no mesh PS")?;
    let name=string_value(ps,t.strings(),"MeshFileName").ok_or("no model name")?;
    let mesh=Mesh::read_xmsh(&vfs.read(vfs.find("data/common/meshes",&compiled_name(name,"_xmsh"))?)?)?;
    let physical=collider::surfaces(&objects,t.strings(),&IDENTITY,false,None,|_|Err("expected native primitive box".into()))?;
    if physical.len()!=1 || !matches!(physical[0],Surface::Mesh(_)) {return Err("expected one native box".into());}
    Ok((t,mesh.bounds().ok_or("empty mesh")?,collider::bounds(&physical).ok_or("empty collider")?))
}
fn spawn(layer:&mut Lrent,t:&Tple,bounds:[f32;6],pos:[f32;3],scale:f32)->Result<usize,Box<dyn std::error::Error>> {
    let i=layer.spawn_template(t,pos,bounds)?;
    let mut world=IDENTITY;
    for k in [0,5,10] {world[k]=scale;}
    world[12..15].copy_from_slice(&pos);
    layer.set_world_matrix(i,world)?;
    let strings=layer.strings().to_vec();
    let e=&mut layer.context_mut().unwrap().entities[i];
    let ps=e.property_sets.iter_mut().find(|s|s.object.class(&strings)=="eCCollisionShape_PS").ok_or("no collider PS")?;
    set_climbable(&mut ps.object,&strings,true)?;
    Ok(i)
}
fn main()->Result<(),Box<dyn std::error::Error>> {
    let game=PathBuf::from(std::env::args().nth(1).ok_or("game path required")?).canonicalize()?;
    let stage=PathBuf::from(std::env::args().nth(2).ok_or("existing build/tests directory required")?).canonicalize()?;
    let allowed=game.join("risen-mods/build/tests").canonicalize()?;
    if stage==allowed || !stage.starts_with(&allowed) {return Err("staging must be inside build/tests".into());}
    let vfs=GameVfs::open(&game)?;
    let original_bytes=vfs.read(vfs.find("data/common/projects","RisenMods_NewLayer_Test.lrent")?)?;
    let original=Lrent::read(&original_bytes)?;
    if original.entities().len()!=35 {return Err("expected the confirmed 35-entity scene".into());}
    let (board,board_visual,board_box)=template(&vfs,"Obj_Furn_Plank_Board_01")?;
    let (crate_model,crate_visual,crate_box)=template(&vfs,"Obj_Dec_Crate_01")?;
    let terrain=Lrent::read(&vfs.read(vfs.find("data/common/projects","Levelmesh_Landscape_COL.lrent")?)?)?;
    let mut cache=HashMap::<String,Arc<CollisionMesh>>::new();
    let mut selected=None;
    // Paired sites near Sara, away from the existing board/tower setups; refuse shoreline/slopes.
    for (a,b,z) in [(-36500.,-35950.,-5400.),(-38500.,-37950.,-5800.),
        (-36500.,-35950.,-5000.),(-38500.,-37950.,-5500.),
        (-36400.,-35850.,-5700.),(-38250.,-37700.,-6200.)] {
        let region=[a-220.,-500.,z-100.,b+220.,2000.,z+160.];
        let occupied=original.entities().iter().skip(1).any(|e| [a,b].into_iter().any(|x| {
            let q=[x-220.,z-100.,x+220.,z+160.];let bb=e.world_box;
            bb[0]<=q[2] && bb[3]>=q[0] && bb[2]<=q[3] && bb[5]>=q[1]
        }));
        if occupied {println!("reject {a}/{b}, z={z}: earlier test objects");continue;}
        let mut surfaces=Vec::new();
        for e in terrain.entities().iter().skip(1) {
            if e.enabled==0 || e.collision_enabled==0 {continue;}
            let objects:Vec<_>=e.property_sets.iter().map(|s|&s.object).collect();
            let found=collider::surfaces(&objects,terrain.strings(),&e.world_matrix,true,Some(region),|name| {
                if let Some(mesh)=cache.get(name) {return Ok(mesh.clone());}
                let mesh=Arc::new(CollisionMesh::read(&vfs.read(vfs.find("data/common/physics",name).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?);
                cache.insert(name.to_owned(),mesh.clone());Ok(mesh)
            })?;
            surfaces.extend(found);
        }
        for s in &mut surfaces {if let Surface::Mesh(m)=s {
            let v=&m.vertices;
            m.triangles.retain(|t|[0,2].into_iter().all(|k| {
                t.iter().map(|&i|v[i as usize][k]).fold(f32::INFINITY,f32::min)<=region[k+3]
                    && t.iter().map(|&i|v[i as usize][k]).fold(f32::NEG_INFINITY,f32::max)>=region[k]
            }));
        }}
        let mut heights=Vec::new();
        for x in [a,b] {for dx in [-210.,-125.,0.,125.,210.] {for dz in [-70.,25.,120.] {
            let y=surfaces.iter().filter_map(|s|collider::VerticalSurface::surface_y(s,x+dx,z+dz,-500.,2000.)).max_by(f32::total_cmp);
            if let Some(y)=y {heights.push(y);}
        }}}
        if heights.len()!=30 {println!("reject {a}/{b}, z={z}: missing terrain");continue;}
        let low=heights.iter().copied().fold(f32::INFINITY,f32::min);
        let high=heights.iter().copied().fold(f32::NEG_INFINITY,f32::max);
        if low<5. || high-low>60. || high>400. {println!("reject {a}/{b}, z={z}: terrain {low}..{high}");continue;}
        selected=Some((a,b,z,low,high));break;
    }
    let (a,b,z,low,high)=selected.ok_or("no safe paired site found; inspect rejected candidates before installation")?;
    let mut layer=original.clone();
    let base_scale=1.2;
    let base_y=high+70.-board_box[1]*base_scale;
    let plate_bottom=base_y+board_box[1]*base_scale;
    let plate_top=base_y+board_box[4]*base_scale;
    let crate_center=[(crate_box[0]+crate_box[3])*0.5,(crate_box[2]+crate_box[5])*0.5];
    let mut csv=String::from("side,x,z,base,post1,post2,pier,board,plate_bottom,plate_top,pier_height,terrain_low,terrain_high\n");
    for (side,x) in [("A",a),("B",b)] {
        let base=spawn(&mut layer,&board,board_visual,[x,base_y,z],base_scale)?;
        let mut posts=Vec::new();
        for dx in [-125.,125.] {
            let pos=[x+dx-crate_center[0]*2.,plate_bottom-crate_box[4]*2.,z+25.-crate_center[1]*2.];
            // Posts extend well below all sampled terrain, while their tops meet the base underside.
            if pos[1]+crate_box[1]*2.>low-5. {return Err("support posts would float above terrain".into());}
            posts.push(spawn(&mut layer,&crate_model,crate_visual,pos,2.)?);
        }
        let pier_scale=0.6;
        let pier=spawn(&mut layer,&crate_model,crate_visual,
            [x+60.-crate_center[0]*pier_scale,plate_top-crate_box[1]*pier_scale,z+20.-crate_center[1]*pier_scale],pier_scale)?;
        // No 3x3 board-grid X coordinate or box vertex may fall in the narrow pier's footprint.
        let pbox=layer.entities()[pier].world_box;
        for px in [board_box[0],(board_box[0]+board_box[3])*0.5,board_box[3]] {
            if x+px>=pbox[0] && x+px<=pbox[3] {return Err("pier overlaps a sample column".into());}
        }
        let board_index=spawn(&mut layer,&board,board_visual,[x,plate_top+150.,z],1.)?;
        let pier_height=(crate_box[4]-crate_box[1])*pier_scale;
        csv.push_str(&format!("{side},{x},{z},{base},{},{},{pier},{board_index},{plate_bottom},{plate_top},{pier_height},{low},{high}\n",posts[0],posts[1]));
        println!("{side}: x={x}, z={z}; base [{base}], posts {posts:?}, narrow pier [{pier}], board [{board_index}]; pier {pier_height:.2} cm");
    }
    // Compare all previous entity bytes, root aggregates, hierarchy and original string indices.
    assert!(layer.strings().starts_with(original.strings()));
    let mut masked=layer.clone();
    masked.archive.strings.truncate(original.strings().len());
    let c=masked.context_mut().unwrap();let old=original.context().unwrap();
    c.entities.truncate(old.entities.len());c.entities[0]=old.entities[0].clone();
    c.hierarchy=old.hierarchy.clone();c.context_box=old.context_box;
    assert_eq!(masked.write(),original_bytes,"previous scene bytes changed");
    let bytes=layer.write();assert_eq!(Lrent::read(&bytes)?.write(),bytes);
    assert_eq!(layer.entities().len(),45);
    OpenOptions::new().write(true).create_new(true).open(stage.join("fixture.lrent"))?.write_all(&bytes)?;
    OpenOptions::new().write(true).create_new(true).open(stage.join("layout.csv"))?.write_all(csv.as_bytes())?;
    assert_eq!(vfs.read(vfs.find("data/common/projects","RisenMods_NewLayer_Test.lrent")?)?,original_bytes);
    println!("STAGED native paired scene; terrain {low:.2}..{high:.2} cm; shared base top {plate_top:.2} cm; original entities unchanged");
    Ok(())
}
