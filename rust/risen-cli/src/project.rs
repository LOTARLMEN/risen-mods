//! World registration is a multi-file edit: validate all output first, back up every original,
//! then atomically replace each file. A failure restores the files already written.
use std::path::{Component, Path, PathBuf};
use std::io::Write;
use risen_formats::lrent::{Lrent, new_guid};
use risen_formats::props::format_guid;
use risen_formats::vfs::{GameVfs, VfsError};
use risen_formats::world::ProjectFile;
use risen_formats::wrldatasc::WorldData;
use serde::{Deserialize, Serialize};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const PROJECTS: &str = "data/common/projects";

pub struct Change {
    pub path: String,
    pub bytes: Vec<u8>,
    pub replace: bool,
}

pub fn plan(vfs: &GameVfs, name: &str, world: &str, donor: &str) -> Result<Vec<Change>> {
    validate_name(name)?;
    for ext in [".sec", ".lrent"] {
        if vfs.files(PROJECTS, ext).any(|e| e.path.rsplit('/').next().is_some_and(|s| s.eq_ignore_ascii_case(&format!("{name}{ext}")))) {
            return Err(format!("{name}{ext} already exists in the mounted game").into());
        }
    }
    let wname = if world.to_ascii_lowercase().ends_with(".wrl") { world.to_string() } else { format!("{world}.wrl") };
    let we = vfs.find(PROJECTS, &wname)?;
    let w = ProjectFile::read(&vfs.read(we)?)?;
    let data_ref = w.world().and_then(|w| w.world_data_file).ok_or("world has no world-data filename")?;
    let de = vfs.find(PROJECTS, w.archive.str(data_ref))?;
    let mut desc = WorldData::read(&vfs.read(de)?)?;
    if desc.sectors().iter().any(|s| s.name.eq_ignore_ascii_case(name)) {
        return Err(format!("sector {name} is already registered").into());
    }
    desc.set_sector(name, true)?;
    let lname = if donor.to_ascii_lowercase().ends_with(".lrent") { donor.to_string() } else { format!("{donor}.lrent") };
    let le = vfs.find(PROJECTS, &lname)?;
    let se = vfs.find(PROJECTS, &format!("{}.sec", &lname[..lname.len() - 6]))?;
    let layer = Lrent::read(&vfs.read(le)?)?.empty_like()?;
    let sector = ProjectFile::read(&vfs.read(se)?)?.sector_like(&[name])?;
    let dir = we.path.rsplit_once('/').ok_or("world has no directory")?.0;
    let layer_bytes = layer.write();
    let sector_bytes = sector.write();
    Lrent::read(&layer_bytes)?;
    ProjectFile::read(&sector_bytes)?;
    let mut changes = vec![
        Change { path: format!("{dir}/RisenMods/{name}.lrent"), bytes: layer_bytes, replace: false },
        Change { path: format!("{dir}/RisenMods/{name}.sec"), bytes: sector_bytes, replace: false },
        Change { path: de.path.clone(), bytes: desc.write(), replace: true },
    ];
    // With a local world list present, gCWorld::OnRead disables any newly listed sector absent
    // from that local list (Game.dll+0x995a42..0x995a76). Enable it there as well.
    let local = format!("{}_local.wrldatasc", de.path.strip_suffix(".wrldatasc").ok_or("world-data filename must end in .wrldatasc")?);
    match vfs.find(PROJECTS, &local) {
        Ok(e) => {
            let mut data = WorldData::read(&vfs.read(e)?)?;
            data.set_sector(name, true)?;
            changes.push(Change { path: e.path.clone(), bytes: data.write(), replace: true });
        }
        Err(VfsError::NotFound(_)) => {}
        Err(e) => return Err(e.into()),
    }
    // This writer uses a game-shaped output tree, so do not silently ignore mount aliases.
    for e in [we, de] {
        if !super::same_file(&e.loose, &vfs.root.join(&e.path)) {
            return Err("project mounts with remapped physical paths are not supported for layer creation".into());
        }
    }
    Ok(changes)
}

fn validate_name(name: &str) -> Result<()> {
    if name.is_empty() || name.len() > 64 || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
        || name.as_bytes()[0].is_ascii_digit()
    { return Err("layer name: 1..64 ASCII letters, digits or underscores; first character cannot be a digit".into()); }
    let upper = name.to_ascii_uppercase();
    if matches!(upper.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || ["COM", "LPT"].iter().any(|p| upper.strip_prefix(p).is_some_and(|n| n.len() == 1 && matches!(n.as_bytes()[0], b'1'..=b'9')))
        || upper.starts_with("SYSDYN")
    { return Err("reserved layer/sector name".into()); }
    Ok(())
}

pub fn ensure_game_closed(root: &Path) -> Result<()> {
    #[cfg(windows)] {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn K32EnumProcesses(pids: *mut u32, size: u32, needed: *mut u32) -> i32;
            fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
            fn QueryFullProcessImageNameW(process: *mut c_void, flags: u32, name: *mut u16, size: *mut u32) -> i32;
            fn CloseHandle(handle: *mut c_void) -> i32;
        }
        // Risen.exe and our risen.exe have the same case-insensitive basename. Only the
        // game's full executable path matters; staging trees without a game need no guard.
        let game = root.join("bin/Risen.exe");
        if !game.is_file() { return Ok(()); }
        let mut pids = vec![0u32; 4096];
        let mut needed = 0;
        if unsafe { K32EnumProcesses(pids.as_mut_ptr(), (pids.len() * 4) as u32, &mut needed) } == 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        if needed as usize == pids.len() * 4 { return Err("process list exceeds guard capacity".into()); }
        for &pid in &pids[..needed as usize / 4] {
            let process = unsafe { OpenProcess(0x1000, 0, pid) }; // PROCESS_QUERY_LIMITED_INFORMATION
            if process.is_null() { continue; }
            let mut name = vec![0u16; 32768];
            let mut size = name.len() as u32;
            let ok = unsafe { QueryFullProcessImageNameW(process, 0, name.as_mut_ptr(), &mut size) };
            unsafe { CloseHandle(process); }
            if ok != 0 {
                let image = PathBuf::from(String::from_utf16_lossy(&name[..size as usize]));
                if image.file_name().is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case("Risen.exe"))
                    && super::same_file(&image, &game)
                { return Err("close Risen before changing world files".into()); }
            }
        }
    }
    #[cfg(not(windows))] let _ = root;
    Ok(())
}

#[derive(Serialize, Deserialize)]
struct Manifest { root: PathBuf, files: Vec<BackupFile> }
#[derive(Serialize, Deserialize)]
struct BackupFile { path: String, existed: bool }

fn checked_path(root: &Path, relative: &str) -> Result<PathBuf> {
    let rel = Path::new(relative);
    if !relative.starts_with("data/common/projects/")
        || rel.components().any(|c| !matches!(c, Component::Normal(_)))
        || ![".lrent", ".sec", ".wrldatasc"].iter().any(|ext| relative.to_ascii_lowercase().ends_with(ext))
    { return Err(format!("unsafe world-edit path: {relative}").into()); }
    let mut path = root.to_path_buf();
    for part in rel.components() {
        path.push(part);
        match std::fs::symlink_metadata(&path) {
            Ok(meta) if meta.file_type().is_symlink() => return Err(format!("refusing symlink: {}", path.display()).into()),
            Ok(_) => {}
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => return Err(e.into()),
        }
    }
    Ok(path)
}

fn read_optional(path: &Path) -> Result<Option<Vec<u8>>> {
    match std::fs::read(path) {
        Ok(bytes) => Ok(Some(bytes)),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e.into()),
    }
}

fn unique() -> String { format_guid(&new_guid(), true).trim_matches(['{', '}']).to_string() }

fn atomic_write(path: &Path, bytes: &[u8]) -> Result<()> {
    let parent = path.parent().ok_or("output has no parent")?;
    std::fs::create_dir_all(parent)?;
    let temp = parent.join(format!(".risen-new-{}", unique()));
    let result = (|| -> Result<()> {
        let mut file = std::fs::OpenOptions::new().write(true).create_new(true).open(&temp)?;
        file.write_all(bytes)?;
        file.sync_all()?;
        drop(file);
        std::fs::rename(&temp, path)?;
        Ok(())
    })();
    if result.is_err() { let _ = std::fs::remove_file(&temp); }
    result
}

/// All new/changed bytes are validated by plan() before this function. Backups also include
/// installed snapshots so restore can refuse to overwrite subsequent edits unless --force.
pub fn apply(root: &Path, changes: &[Change]) -> Result<PathBuf> {
    apply_with(root, changes, atomic_write)
}

fn apply_with(root: &Path, changes: &[Change], mut write: impl FnMut(&Path, &[u8]) -> Result<()>) -> Result<PathBuf> {
    ensure_game_closed(root)?;
    let root = std::fs::canonicalize(root)?;
    let paths: Vec<_> = changes.iter().map(|c| checked_path(&root, &c.path)).collect::<Result<_>>()?;
    let before: Vec<_> = paths.iter().map(|p| read_optional(p)).collect::<Result<_>>()?;
    for (i, change) in changes.iter().enumerate() {
        if !change.replace && before[i].is_some() { return Err(format!("{} already exists", paths[i].display()).into()); }
        if changes[..i].iter().any(|c| c.path.eq_ignore_ascii_case(&change.path)) { return Err("duplicate output path".into()); }
    }
    let backup = root.join("risen-mods/backup/world-edits").join(unique());
    std::fs::create_dir_all(&backup)?;
    for (i, change) in changes.iter().enumerate() {
        if let Some(bytes) = &before[i] {
            let path = backup.join(format!("{i}.before"));
            std::fs::write(&path, bytes)?;
            if std::fs::read(&path)? != *bytes { return Err("backup verification failed".into()); }
        }
        let after = backup.join(format!("{i}.after"));
        std::fs::write(&after, &change.bytes)?;
        if std::fs::read(&after)? != change.bytes { return Err("installed snapshot verification failed".into()); }
    }
    let manifest = Manifest { root, files: changes.iter().zip(&before).map(|(c, b)| BackupFile { path: c.path.clone(), existed: b.is_some() }).collect() };
    std::fs::write(backup.join("manifest.json"), serde_json::to_vec_pretty(&manifest)?)?;
    ensure_game_closed(&manifest.root)?;
    let mut applied = 0;
    let result = (|| -> Result<()> {
        for (i, change) in changes.iter().enumerate() {
            if read_optional(&paths[i])? != before[i] { return Err(format!("{} changed during preparation", paths[i].display()).into()); }
            write(&paths[i], &change.bytes)?;
            applied += 1;
            if std::fs::read(&paths[i])? != change.bytes { return Err("written file verification failed".into()); }
        }
        Ok(())
    })();
    if let Err(error) = result {
        let mut failures = Vec::new();
        for i in (0..applied).rev() {
            let rollback = (|| -> Result<()> {
                if read_optional(&paths[i])?.as_ref() != Some(&changes[i].bytes) { return Err("file changed after write".into()); }
                match &before[i] {
                    Some(bytes) => atomic_write(&paths[i], bytes)?,
                    None => std::fs::remove_file(&paths[i])?,
                }
                Ok(())
            })();
            if let Err(e) = rollback { failures.push(format!("{}: {e}", paths[i].display())); }
        }
        return Err(format!("{error}; rollback failures: {failures:?}; backup: {}", backup.display()).into());
    }
    Ok(backup)
}

pub fn restore(backup: &Path, force: bool) -> Result<()> {
    let m: Manifest = serde_json::from_slice(&std::fs::read(backup.join("manifest.json"))?)?;
    let root = std::fs::canonicalize(&m.root)?;
    ensure_game_closed(&root)?;
    let mut files = Vec::new();
    for (i, file) in m.files.iter().enumerate() {
        let path = checked_path(&root, &file.path)?;
        let before = if file.existed { Some(std::fs::read(backup.join(format!("{i}.before")))?) } else { None };
        let current = read_optional(&path)?;
        if current == before { continue; }
        if !force && current.as_ref() != Some(&std::fs::read(backup.join(format!("{i}.after")))?) {
            return Err(format!("{} has subsequent edits; use --force to restore anyway", path.display()).into());
        }
        files.push((path, before, current));
    }
    ensure_game_closed(&root)?;
    for (path, before, expected) in files.into_iter().rev() {
        if read_optional(&path)? != expected { return Err(format!("{} changed during restore", path.display()).into()); }
        match before {
            Some(bytes) => atomic_write(&path, &bytes)?,
            None => { if path.exists() { std::fs::remove_file(&path)?; } }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Temp(PathBuf);
    impl Temp {
        fn new() -> Self {
            let p = std::env::temp_dir().join(format!("risen-project-test-{}", unique()));
            std::fs::create_dir(&p).unwrap();
            Self(p)
        }
        fn file(&self, name: &str, bytes: &[u8]) {
            let p = self.0.join(name);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, bytes).unwrap();
        }
    }
    impl Drop for Temp {
        fn drop(&mut self) {
            let path = std::fs::canonicalize(&self.0).unwrap();
            assert_eq!(path.parent(), Some(std::fs::canonicalize(std::env::temp_dir()).unwrap().as_path()));
            assert!(path.file_name().unwrap().to_string_lossy().starts_with("risen-project-test-"));
            std::fs::remove_dir_all(path).unwrap();
        }
    }
    fn change(path: &str, bytes: &[u8], replace: bool) -> Change {
        Change { path: format!("data/common/projects/{path}"), bytes: bytes.to_vec(), replace }
    }
    #[test]
    fn backup_restore_preserves_originals_and_removes_created_files() {
        let t = Temp::new();
        t.file("data/common/projects/World.wrldatasc", b"original");
        t.file("data/common/projects/unrelated.sec", b"untouched");
        let changes = [change("New.lrent", b"layer", false), change("New.sec", b"sector", false), change("World.wrldatasc", b"updated", true)];
        let backup = apply(&t.0, &changes).unwrap();
        assert_eq!(std::fs::read(backup.join("2.before")).unwrap(), b"original");
        restore(&backup, false).unwrap();
        assert!(!t.0.join("data/common/projects/New.lrent").exists());
        assert!(!t.0.join("data/common/projects/New.sec").exists());
        assert_eq!(std::fs::read(t.0.join("data/common/projects/World.wrldatasc")).unwrap(), b"original");
        assert_eq!(std::fs::read(t.0.join("data/common/projects/unrelated.sec")).unwrap(), b"untouched");
        restore(&backup, false).unwrap(); // idempotent
    }
    #[test]
    fn later_failure_rolls_back_existing_and_new_files() {
        let t = Temp::new();
        t.file("data/common/projects/World.wrldatasc", b"original");
        let changes = [change("World.wrldatasc", b"updated", true), change("New.sec", b"sector", false), change("New.lrent", b"layer", false)];
        let mut calls = 0;
        let error = apply_with(&t.0, &changes, |p, bytes| {
            calls += 1;
            if calls == 3 { return Err("injected write failure".into()); }
            atomic_write(p, bytes)
        }).unwrap_err();
        assert!(error.to_string().contains("rollback failures: []"));
        assert_eq!(std::fs::read(t.0.join("data/common/projects/World.wrldatasc")).unwrap(), b"original");
        assert!(!t.0.join("data/common/projects/New.sec").exists());
        assert!(!t.0.join("data/common/projects/New.lrent").exists());
    }
    #[test]
    fn restore_refuses_subsequent_edits_before_touching_any_files() {
        let t = Temp::new();
        t.file("data/common/projects/World.wrldatasc", b"original");
        let changes = [change("New.sec", b"sector", false), change("World.wrldatasc", b"updated", true)];
        let backup = apply(&t.0, &changes).unwrap();
        t.file("data/common/projects/World.wrldatasc", b"later edit");
        assert!(restore(&backup, false).is_err());
        assert!(t.0.join("data/common/projects/New.sec").exists());
        assert_eq!(std::fs::read(t.0.join("data/common/projects/World.wrldatasc")).unwrap(), b"later edit");
        restore(&backup, true).unwrap();
        assert_eq!(std::fs::read(t.0.join("data/common/projects/World.wrldatasc")).unwrap(), b"original");
    }
    #[test]
    fn collisions_traversal_and_reserved_names_are_refused() {
        let t = Temp::new();
        t.file("data/common/projects/New.sec", b"original");
        assert!(apply(&t.0, &[change("New.sec", b"overwrite", false)]).is_err());
        assert!(!t.0.join("risen-mods").exists());
        assert!(checked_path(&t.0, "data/common/projects/../../outside.sec").is_err());
        assert!(checked_path(&t.0, "bin/Risen.exe").is_err());
        for name in ["CON", "LPT1", "SysDyn_New", "../X", "x.sec", "", "1Name"] { assert!(validate_name(name).is_err()); }
        assert!(validate_name("RisenMods_Test").is_ok());
    }
    #[test]
    #[cfg(windows)]
    fn process_guard_uses_full_executable_path() {
        use std::os::windows::process::CommandExt;
        use std::io::BufRead;
        struct Running(std::process::Child);
        impl Drop for Running {
            fn drop(&mut self) { let _ = self.0.kill(); let _ = self.0.wait(); }
        }
        let t = Temp::new();
        let helper = std::env::current_exe().unwrap();
        let start = |dir: &str| {
            let bin = t.0.join(dir);
            std::fs::create_dir(&bin).unwrap();
            let exe = bin.join("Risen.exe");
            std::fs::copy(&helper, &exe).unwrap();
            let mut child = Running(std::process::Command::new(exe)
                .args(["--exact", "project::tests::process_guard_child", "--ignored", "--nocapture"])
                .env("RISEN_PROCESS_GUARD_CHILD", "1")
                .stdin(std::process::Stdio::piped()).stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::null()).creation_flags(0x08000000).spawn().unwrap());
            let mut out = std::io::BufReader::new(child.0.stdout.take().unwrap());
            loop {
                let mut line = String::new();
                assert!(out.read_line(&mut line).unwrap() > 0, "guard helper exited before readiness");
                if line.contains("GUARD_READY") { break; }
            }
            child
        };
        let other = start("tools");
        std::fs::create_dir(t.0.join("bin")).unwrap();
        std::fs::copy(&helper, t.0.join("bin/Risen.exe")).unwrap();
        assert!(ensure_game_closed(&t.0).is_ok()); // same name, wrong path
        // start() expects a new directory; the first guard only needed the file to exist.
        std::fs::remove_file(t.0.join("bin/Risen.exe")).unwrap();
        std::fs::remove_dir(t.0.join("bin")).unwrap();
        let game = start("bin");
        assert!(ensure_game_closed(&t.0).is_err());
        drop(game);
        assert!(ensure_game_closed(&t.0).is_ok());
        drop(other);
    }
    #[test]
    #[ignore = "helper process for process_guard_uses_full_executable_path"]
    fn process_guard_child() {
        if std::env::var("RISEN_PROCESS_GUARD_CHILD").as_deref() != Ok("1") { return; }
        use std::io::Read;
        std::io::stdout().write_all(b"GUARD_READY\n").unwrap();
        std::io::stdout().flush().unwrap();
        let _ = std::io::stdin().read_exact(&mut [0]);
    }
}
