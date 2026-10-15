//! Small independent G3V0 fixtures; no installed game required and no game files changed.
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use risen_formats::bin_io::Writer;
use risen_formats::vfs::{GameVfs, Source};

static NEXT: AtomicU64 = AtomicU64::new(0);

struct Fixture(PathBuf);
impl Fixture {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!("risen-vfs-test-{}-{}", std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
        std::fs::create_dir(&path).unwrap();
        std::fs::create_dir(path.join("bin")).unwrap();
        Self(path)
    }
    fn file(&self, path: &str, bytes: &[u8]) {
        let path = self.0.join(path);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }
    fn mounts(&self, flags: &str, mounts: &str) -> GameVfs {
        self.file("bin/mountlist_packed.ini", format!("[Root]\nRootPath=..\n{flags}\n{mounts}\n").as_bytes());
        GameVfs::open(&self.0).unwrap()
    }
    fn pak(&self, path: &str, name: &str, data: &[u8], time: u64) {
        self.file(path, &pak(name, data, time));
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        assert_eq!(self.0.parent(), Some(std::env::temp_dir().as_path()));
        std::fs::remove_dir_all(&self.0).unwrap();
    }
}

/// One flat stored file, built from the documented on-disk layout, not the reader's structs.
fn pak(name: &str, data: &[u8], time: u64) -> Vec<u8> {
    let mut d = Writer::default();
    d.u32(0); // root name
    d.u64(0); d.u64(0); d.u64(0);
    d.u32(16); d.u32(1); // root directory, one child
    d.u32(0); // child type
    d.u32(name.len() as u32); d.bytes(name.as_bytes()); d.u8(0);
    d.u64(48); // payload offset
    d.u64(0); d.u64(0); d.u64(time);
    d.u32(0); d.u32(0); d.u32(0); // attributes, encryption, compression
    d.u32(data.len() as u32); d.u32(data.len() as u32);
    let mut w = Writer::default();
    w.u32(1); w.bytes(b"G3V0"); w.u64(0); w.u32(0); w.u32(0);
    w.u64(48); w.u64(48 + data.len() as u64); w.u64(48 + data.len() as u64 + d.buf.len() as u64);
    w.bytes(data); w.bytes(&d.buf);
    w.buf
}

#[test]
fn loose_overrides_and_new_files_with_case_and_separators() {
    let f = Fixture::new();
    f.pak("data/common/projects.pak", "old.lrent", b"packed", 200);
    f.file("data/common/projects/old.lrent", b"loose");
    f.file("data/common/projects/Sub/New.lrent", b"new");
    let v = f.mounts("NoPhysical=false", "[Packed]\ndata/common/projects");
    assert_eq!(v.read(v.find("data/common/projects", "OLD.LRENT").unwrap()).unwrap(), b"loose");
    let e = v.find("DATA/COMMON/PROJECTS", "#G3:\\DATA\\COMMON\\PROJECTS\\sub\\new.lrent").unwrap();
    assert_eq!(v.read(e).unwrap(), b"new");
    assert!(matches!(e.source, Source::Loose(_)));
    assert_eq!(v.files("data/common/projects", ".LRENT").count(), 2);
}

#[test]
fn later_archive_wins_despite_older_timestamp_and_chain_stops_at_gap() {
    let f = Fixture::new();
    f.pak("data/projects.pak", "test.lrent", b"base", 200);
    f.pak("data/projects.p00", "test.lrent", b"patch", 100);
    f.pak("data/projects.p02", "test.lrent", b"unreachable", 300);
    let v = f.mounts("", "[Packed]\ndata/projects");
    let e = v.find("data/projects", "test.lrent").unwrap();
    assert_eq!(v.read(e).unwrap(), b"patch");
    assert!(v.source(e).contains("projects.p00"));
    assert_eq!(v.archive_count(), 2);
}

#[test]
fn archive_requires_base_and_unmounted_paks_are_ignored() {
    let f = Fixture::new();
    f.pak("data/projects.p00", "patch.lrent", b"orphan", 100);
    f.pak("data/unmounted.pak", "other.lrent", b"unmounted", 100);
    let v = f.mounts("", "[Packed]\ndata/projects");
    assert_eq!(v.archive_count(), 0);
    assert!(v.entries.is_empty());
}

#[test]
fn flags_affect_packed_mounts_but_not_explicit_physical_mounts() {
    let f = Fixture::new();
    f.pak("data/projects.pak", "test.lrent", b"packed", 100);
    f.file("data/projects/test.lrent", b"loose");
    f.file("data/ini/Config.xml", b"physical");
    let mounts = "[Physical]\nini=data/ini\n[Packed]\ndata/projects";
    let packed = f.mounts("NoPhysical=true", mounts);
    assert_eq!(packed.read(packed.find("data/projects", "test.lrent").unwrap()).unwrap(), b"packed");
    assert!(packed.find("ini", "config.xml").is_ok());
    let loose = f.mounts("NoPacked=true", mounts);
    assert_eq!(loose.archive_count(), 0);
    assert_eq!(loose.read(loose.find("data/projects", "test.lrent").unwrap()).unwrap(), b"loose");
    let neither = f.mounts("NoPacked=true\nNoPhysical=true", mounts);
    assert!(neither.find("data/projects", "test.lrent").is_err());
    assert!(neither.find("ini", "config.xml").is_ok());
}

#[test]
fn wildcard_archives_and_loose_only_directories() {
    let f = Fixture::new();
    f.pak("data/speech_en.pak", "sound.dat", b"english", 100);
    f.pak("data/speech_ru.pak", "sound.dat", b"russian", 100);
    f.file("data/speech_mod/new.dat", b"loose-only");
    let v = f.mounts("", "[Packed]\ndata/speech_*");
    assert_eq!(v.archive_count(), 2);
    assert_eq!(v.read(v.find("data/speech_en", "sound.dat").unwrap()).unwrap(), b"english");
    assert_eq!(v.read(v.find("data/speech_mod", "new.dat").unwrap()).unwrap(), b"loose-only");
    assert!(v.find("data", "sound.dat").is_err()); // no arbitrary basename winner
}

#[test]
fn ambiguous_names_require_exact_paths() {
    let f = Fixture::new();
    f.file("data/projects/A/test.lrent", b"a");
    f.file("data/projects/B/test.lrent", b"b");
    f.file("data/projects/test.lrent", b"root");
    let v = f.mounts("", "[Packed]\ndata/projects");
    assert!(v.find("data/projects", "test.lrent").unwrap_err().to_string().contains("ambiguous"));
    assert_eq!(v.read(v.find("data/projects", "A/test.lrent").unwrap()).unwrap(), b"a");
    assert_eq!(v.read(v.find("data/projects", "./test.lrent").unwrap()).unwrap(), b"root");
    assert_eq!(v.files("", ".lrent").count(), 3);
    assert!(v.find("data/projects", "../outside.lrent").is_err());
    assert!(v.find("data/projects", "data/projects2/A/test.lrent").is_err());
}

#[test]
fn aliases_map_reads_and_write_destinations() {
    let f = Fixture::new();
    f.pak("data/projects.pak", "test.lrent", b"aliased", 100);
    let v = f.mounts("", "[Packed]\nworlds=data/projects");
    let e = v.find("worlds", "test.lrent").unwrap();
    assert_eq!(e.path, "worlds/test.lrent");
    assert_eq!(e.loose, std::fs::canonicalize(&f.0).unwrap().join("data/projects/test.lrent"));
    assert_eq!(v.read(e).unwrap(), b"aliased");
}

#[test]
fn archive_traversal_is_rejected() {
    let f = Fixture::new();
    f.pak("data/projects.pak", "../escape.lrent", b"bad", 100);
    f.file("bin/mountlist_packed.ini", b"[Root]\nRootPath=..\n[Packed]\ndata/projects\n");
    assert!(GameVfs::open(&f.0).is_err());
    assert!(!Path::new(&f.0).join("escape.lrent").exists());
}
