//! Explicitly scoped staged installation of static mesh/collision resources and world registration.
use std::path::{Component, Path, PathBuf};
use risen_formats::{archive::Archive, bin_io::Reader, geometry::Mesh, lrent::Lrent,
    props::PropertyObject, world::ProjectFile, wrldatasc::WorldData, xcom::CollisionMesh};
use serde::Deserialize;
use crate::project::{self, Change, Scope};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

#[derive(Deserialize)]
pub struct Bundle { pub files: Vec<File> }
#[derive(Deserialize)]
pub struct File { pub path: String, pub source: String, pub before: Option<String> }

fn source(stage: &Path, name: &str) -> Result<Vec<u8>> {
    if name.contains(['\\', ':']) || name.split('/').any(|s| s.is_empty() || s == "." || s == "..")
        || Path::new(name).components().any(|c| !matches!(c, Component::Normal(_))) {
        return Err("bundle source must be a relative file inside its staging directory".into());
    }
    let mut path = stage.to_path_buf();
    for p in Path::new(name).components() {
        path.push(p);
        if project::is_reparse(&std::fs::symlink_metadata(&path)?) { return Err("bundle source uses a reparse point".into()); }
    }
    Ok(std::fs::read(path)?)
}

fn validate(path: &str, bytes: &[u8]) -> Result<()> {
    if path.ends_with(".lrent") { Lrent::read(bytes)?; }
    else if path.ends_with(".sec") { ProjectFile::read(bytes)?; }
    else if path.ends_with(".wrldatasc") { WorldData::read(bytes)?; }
    else if path.ends_with("._xmsh") { Mesh::read_xmsh(bytes)?; }
    else if path.ends_with("._xcom") { CollisionMesh::read(bytes)?; }
    else if matches!(path, "data/common/meshes/compiled_meshes.bin" | "data/common/physics/compiled_collision_meshes.bin") {
        let collision = path.contains("/physics/");
        let a = Archive::read(bytes)?;
        let mut r = Reader::new(&a.body);
        if r.bytes(4)? != b"GH04" { return Err("unsupported mesh cache magic".into()); }
        let count = r.u32()?;
        if count as usize > r.remaining()/27 { return Err("invalid mesh cache count".into()); }
        let mut names = std::collections::HashSet::new();
        for _ in 0..count {
            if r.bytes(4)? != if collision { b"CM00" } else { b"MS02" } { return Err("unsupported mesh cache data revision".into()); }
            let o = PropertyObject::read(&mut r, &a.strings)?;
            if o.class(&a.strings) != if collision { "eCCollisionMeshResource2" } else { "eCMeshResource2" } { return Err("unexpected mesh cache class".into()); }
            let name = a.strings.get(r.u16()? as usize).ok_or("invalid mesh cache name index")?;
            if !names.insert(name.to_ascii_lowercase()) { return Err("duplicate mesh cache resource name".into()); }
            r.bytes(20)?;
        }
        if r.remaining() != 0 { return Err("bytes after mesh cache".into()); }
    } else { return Err("unsupported bundle file".into()); }
    Ok(())
}

pub fn plan(root: &Path, manifest: &Path) -> Result<(Vec<Change>, Vec<Option<Vec<u8>>>)> {
    let manifest = manifest.canonicalize()?;
    let stage = manifest.parent().ok_or("bundle needs staging directory")?;
    let bundle: Bundle = serde_json::from_slice(&std::fs::read(&manifest)?)?;
    if bundle.files.is_empty() { return Err("empty asset bundle".into()); }
    let mut changes: Vec<Change> = Vec::new(); let mut expected = Vec::new();
    for file in bundle.files {
        let path = project::scoped_path(root, &file.path, Scope::Assets)?;
        if changes.iter().any(|c| c.path.eq_ignore_ascii_case(&file.path)) { return Err("duplicate bundle target".into()); }
        let bytes = source(stage, &file.source)?;
        validate(&file.path, &bytes)?;
        let before = file.before.as_deref().map(|p| source(stage, p)).transpose()?;
        let current = match std::fs::read(&path) { Ok(b) => Some(b), Err(e) if e.kind() == std::io::ErrorKind::NotFound => None, Err(e) => return Err(e.into()) };
        if before != current { return Err(format!("{} changed since staging; rebuild the bundle", file.path).into()); }
        changes.push(Change { path: file.path, bytes, replace: before.is_some() }); expected.push(before);
    }
    Ok((changes, expected))
}

pub fn install(root: &Path, manifest: &Path, dry_run: bool) -> Result<Option<PathBuf>> {
    let (changes, expected) = plan(root, manifest)?;
    for c in &changes { println!("{} {} ({} bytes)", if c.replace { "update" } else { "create" }, c.path, c.bytes.len()); }
    if dry_run { return Ok(None); }
    Ok(Some(project::apply_assets(root, &changes, &expected)?))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn malformed_resources_are_refused_and_world_data_is_parsed() {
        for path in ["data/common/meshes/compiled_meshes.bin", "data/common/meshes/a._xmsh",
            "data/common/physics/a._xcom", "data/common/projects/a.lrent", "data/common/projects/a.sec"] {
            assert!(validate(path, b"bad").is_err());
        }
        assert!(validate("data/common/projects/World.wrldatasc", b"[Sector.List]\nExample=true\n").is_ok());
    }
}
