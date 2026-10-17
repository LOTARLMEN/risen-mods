//! Stage reversible disabling of the old test sector. No writes to the game.
use risen_formats::{props::ClassData, vfs::{GameVfs,VfsError}, world::ProjectFile, wrldatasc::WorldData};
use std::{io::Write, path::PathBuf};
fn main() -> Result<(),Box<dyn std::error::Error>> {
    let root=PathBuf::from(std::env::args_os().nth(1).ok_or("game root")?).canonicalize()?;
    let stage=PathBuf::from(std::env::args_os().nth(2).ok_or("stage")?).canonicalize()?;
    let allowed=root.join("risen-mods/build/tests").canonicalize()?;
    if stage==allowed || !stage.starts_with(&allowed){return Err("stage must be a child of build/tests".into());}
    let vfs=GameVfs::open(&root)?;
    let old="RisenMods_NewLayer_Test";
    let se=vfs.find("data/common/projects",&format!("{old}.sec"))?;
    let mut sector=ProjectFile::read(&vfs.read(se)?)?;
    let strings=sector.archive.strings.clone();
    let body=sector.root.body.as_mut().ok_or("empty sector")?;
    let ClassData::Sector(s)=&mut body.data else{return Err("not a sector".into());};
    s.enabled=Some(0);
    for p in &mut body.props.list {if strings[p.name.0 as usize]=="EnabledOnStartup"{
        if strings[p.ty.0 as usize]!="bool" || p.value.len()!=1{return Err("invalid startup flag".into());}
        p.value[0]=0;
    }}
    let bytes=sector.write();assert_eq!(ProjectFile::read(&bytes)?.write(),bytes);
    let mut changes=vec![(se,bytes)];
    for name in ["World.wrldatasc","World_local.wrldatasc"]{
        let e=match vfs.find("data/common/projects",name){Ok(e)=>e,Err(VfsError::NotFound(_)) if name.contains("_local")=>continue,Err(e)=>return Err(e.into())};
        let mut data=WorldData::read(&vfs.read(e)?)?;
        data.set_sector(old,false)?;data.set_sector("RisenMods_Assets_Test",true)?;
        changes.push((e,data.write()));
    }
    let mut list=String::new();
    for (i,(e,bytes)) in changes.into_iter().enumerate(){
        let after=format!("{i}.after.bin");
        std::fs::OpenOptions::new().write(true).create_new(true).open(stage.join(&after))?.write_all(&bytes)?;
        let before=if e.loose.is_file(){
            let file=format!("{i}.before.bin");std::fs::copy(&e.loose,stage.join(&file))?;file
        }else{String::new()};
        list.push_str(&format!("{}\t{after}\t{before}\n",e.path));
    }
    std::fs::OpenOptions::new().write(true).create_new(true).open(stage.join("changes.tsv"))?.write_all(list.as_bytes())?;
    Ok(())
}
