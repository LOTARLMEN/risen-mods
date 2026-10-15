//! Mounted game assets: mountlist_packed.ini, G3V0 archives and loose overrides.
//! Uses virtual paths, not a global basename cache: ambiguous short names are refused.
//! Archives in a chain are contiguous (.pak, .p00, .p01, ...); among packed copies of
//! the same virtual path the later archive wins, even with an older entry FILETIME. Loose wins.
//! NoPhysical/NoPacked apply to [Packed]; [Physical] and [Bin] are directory mounts.

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};

use crate::pak::{Pak, PakError};

#[derive(Debug, thiserror::Error)]
pub enum VfsError {
    #[error("{0}")]
    Io(#[from] std::io::Error),
    #[error("{0}")]
    Pak(#[from] PakError),
    #[error("{0}")]
    Invalid(String),
    #[error("{0}")]
    NotFound(String),
}

#[derive(Debug, Clone)]
pub enum Source {
    Packed { archive: usize, entry: usize },
    Loose(PathBuf),
}

#[derive(Debug, Clone)]
pub struct VfsEntry {
    /// Full virtual path, '/' separated; original case kept.
    pub path: String,
    pub size: u64,
    pub source: Source,
    /// Physical path for a loose override, also when the current source is packed.
    pub loose: PathBuf,
}

pub struct GameVfs {
    pub root: PathBuf,
    pub mountlist: PathBuf,
    pub no_physical: bool,
    pub no_packed: bool,
    /// Effective files, sorted by case-insensitive virtual path.
    pub entries: Vec<VfsEntry>,
    paks: Vec<Pak>,
    index: HashMap<String, usize>,
}

#[derive(Debug)]
struct Mount {
    virtual_path: String,
    physical: PathBuf,
    packed: bool,
}

fn invalid(message: impl Into<String>) -> VfsError {
    VfsError::Invalid(message.into())
}

/// Canonical virtual key. Reject traversal rather than letting archive paths escape a mount.
fn normalize(path: &str) -> Result<String, VfsError> {
    let path = path.replace('\\', "/");
    let path = if path.get(..4).is_some_and(|p| p.eq_ignore_ascii_case("#G3:")) { &path[4..] } else { &path };
    let mut parts = Vec::new();
    for p in path.split('/') {
        match p {
            "" | "." => {},
            ".." => return Err(invalid(format!("virtual path contains '..': {path}"))),
            p if p.contains(':') => return Err(invalid(format!("invalid virtual path: {path}"))),
            p => parts.push(p),
        }
    }
    Ok(parts.join("/"))
}

fn under(path: &str, prefix: &str) -> bool {
    prefix.is_empty() || path == prefix || path.strip_prefix(prefix).is_some_and(|s| s.starts_with('/'))
}

fn wildcard(pattern: &str, text: &str) -> bool {
    let (p, t) = (pattern.to_ascii_lowercase(), text.to_ascii_lowercase());
    let (p, t) = (p.as_bytes(), t.as_bytes());
    let (mut i, mut j, mut star, mut retry) = (0, 0, None, 0);
    while j < t.len() {
        if i < p.len() && (p[i] == b'?' || p[i] == t[j]) {
            i += 1;
            j += 1;
        } else if i < p.len() && p[i] == b'*' {
            star = Some(i);
            i += 1;
            retry = j;
        } else if let Some(s) = star {
            retry += 1;
            j = retry;
            i = s + 1;
        } else {
            return false;
        }
    }
    while i < p.len() && p[i] == b'*' { i += 1; }
    i == p.len()
}

fn expand_env(value: &str) -> Result<String, VfsError> {
    let mut out = String::new();
    let mut rest = value;
    while let Some(i) = rest.find("$(") {
        out.push_str(&rest[..i]);
        let end = rest[i + 2..].find(')').ok_or_else(|| invalid(format!("unclosed variable: {value}")))? + i + 2;
        let name = &rest[i + 2..end];
        let replacement = std::env::var(name).or_else(|_| {
            std::env::vars().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v).ok_or(std::env::VarError::NotPresent)
        }).map_err(|_| invalid(format!("mountlist variable {name} is not set")))?;
        out.push_str(&replacement);
        rest = &rest[end + 1..];
    }
    out.push_str(rest);
    Ok(out.replace('\\', "/"))
}

fn bool_value(value: &str) -> Result<bool, VfsError> {
    match value.to_ascii_lowercase().as_str() {
        "true" | "1" => Ok(true),
        "false" | "0" => Ok(false),
        _ => Err(invalid(format!("invalid mountlist boolean: {value}"))),
    }
}

impl GameVfs {
    pub fn open(game: impl AsRef<Path>) -> Result<Self, VfsError> {
        Self::from_mountlist(game.as_ref().join("bin/mountlist_packed.ini"))
    }

    pub fn from_mountlist(path: impl AsRef<Path>) -> Result<Self, VfsError> {
        let mountlist = std::fs::canonicalize(path)?;
        let bin = mountlist.parent().ok_or_else(|| invalid("mountlist has no parent"))?;
        let text = std::fs::read_to_string(&mountlist)?;
        let mut section = String::new();
        let mut root_path = "..".to_string();
        let (mut no_physical, mut no_packed) = (false, false);
        let mut lines = Vec::new();
        for line in text.trim_start_matches('\u{feff}').lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with([';', '#']) { continue; }
            if line.starts_with('[') && line.ends_with(']') {
                section = line[1..line.len() - 1].trim().to_ascii_lowercase();
                continue;
            }
            let (key, value) = line.split_once('=').map_or((line, line), |(k, v)| (k.trim(), v.trim()));
            if section == "root" {
                match key.to_ascii_lowercase().as_str() {
                    "rootpath" => root_path = expand_env(value)?,
                    "nophysical" => no_physical = bool_value(value)?,
                    "nopacked" => no_packed = bool_value(value)?,
                    _ => {},
                }
            } else if matches!(section.as_str(), "packed" | "physical" | "bin") {
                lines.push((section.clone(), key.to_string(), value.to_string()));
            } else {
                return Err(invalid(format!("unsupported mountlist section [{section}]")));
            }
        }
        let root = std::fs::canonicalize(bin.join(root_path))?;
        let mut mounts = Vec::new();
        for (section, key, value) in lines {
            let physical = if section == "bin" { bin } else { &root }.join(expand_env(&value)?);
            let key = normalize(&key)?;
            let virtual_path = if section == "bin" { format!("bin/{key}") } else { key };
            expand_mount(&virtual_path, &physical, section == "packed", &mut mounts)?;
        }
        let mut vfs = Self { root, mountlist, no_physical, no_packed, entries: Vec::new(), paks: Vec::new(), index: HashMap::new() };
        let mut files = BTreeMap::new();
        // Mount section order in Engine.dll: Bin, Physical, Packed. Within Packed the native
        // implementation registers directories before archives; loose sources have precedence.
        for mount in &mounts {
            if !mount.packed || no_packed { continue; }
            for n in 0usize.. {
                let extension = if n == 0 { "pak".to_string() } else { format!("p{:02}", n - 1) };
                let archive_path = PathBuf::from(format!("{}.{extension}", mount.physical.display()));
                if !archive_path.is_file() { break; }
                let pak = Pak::open(&archive_path)?;
                let id = vfs.paks.len();
                vfs.paks.push(pak);
                for (i, e) in vfs.paks[id].entries.iter().enumerate() {
                    let relative = normalize(&e.path)?;
                    let path = format!("{}/{relative}", mount.virtual_path);
                    let key = path.to_ascii_lowercase();
                    files.insert(key, VfsEntry {
                        path, size: e.size as u64, source: Source::Packed { archive: id, entry: i },
                        loose: mount.physical.join(relative),
                    });
                }
            }
        }
        for mount in &mounts {
            if mount.packed && no_physical { continue; }
            if mount.physical.is_dir() {
                collect_loose(&mount.physical, &mount.physical, &mount.virtual_path, &mut files)?;
            }
        }
        vfs.entries = files.into_values().collect();
        vfs.index = vfs.entries.iter().enumerate().map(|(i, e)| (e.path.to_ascii_lowercase(), i)).collect();
        Ok(vfs)
    }

    pub fn archive_count(&self) -> usize { self.paks.len() }

    pub fn files<'a>(&'a self, scope: &str, extension: &str) -> impl Iterator<Item = &'a VfsEntry> {
        let scope = scope.replace('\\', "/").trim_end_matches('/').to_ascii_lowercase();
        let extension = extension.to_ascii_lowercase();
        self.entries.iter().filter(move |e| {
            let path = e.path.to_ascii_lowercase();
            under(&path, &scope) && path.ends_with(&extension)
        })
    }

    /// Full virtual path, path relative to scope, or unique basename within scope.
    pub fn find(&self, scope: &str, spec: &str) -> Result<&VfsEntry, VfsError> {
        let explicit_path = spec.contains(['/', '\\']);
        let scope = normalize(scope)?.to_ascii_lowercase();
        let spec = normalize(spec)?.to_ascii_lowercase();
        if explicit_path {
            for key in [spec.clone(), format!("{scope}/{spec}")] {
                if let Some(&i) = self.index.get(&key) && under(&key, &scope) { return Ok(&self.entries[i]); }
            }
        }
        let mut hits = self.files(&scope, "").filter(|e| {
            !spec.contains('/') && e.path.rsplit('/').next().is_some_and(|name| name.eq_ignore_ascii_case(&spec))
        });
        let first = hits.next().ok_or_else(|| VfsError::NotFound(format!("{spec}: not found under {scope}")))?;
        if let Some(second) = hits.next() {
            return Err(invalid(format!("{spec}: ambiguous name; use a full path: {}, {}", first.path, second.path)));
        }
        Ok(first)
    }

    pub fn read(&self, e: &VfsEntry) -> Result<Vec<u8>, VfsError> {
        match &e.source {
            Source::Loose(path) => Ok(std::fs::read(path)?),
            Source::Packed { archive, entry } => Ok(self.paks[*archive].read(&self.paks[*archive].entries[*entry])?),
        }
    }

    pub fn source(&self, e: &VfsEntry) -> String {
        match &e.source {
            Source::Loose(path) => path.display().to_string(),
            Source::Packed { archive, entry } => format!("{}:{}", self.paks[*archive].path.display(), self.paks[*archive].entries[*entry].path),
        }
    }
}

fn expand_mount(virtual_path: &str, physical: &Path, packed: bool, out: &mut Vec<Mount>) -> Result<(), VfsError> {
    let leaf = physical.file_name().and_then(|s| s.to_str()).ok_or_else(|| invalid("mount has no file name"))?;
    if !leaf.contains(['*', '?']) {
        out.push(Mount { virtual_path: virtual_path.to_string(), physical: physical.to_path_buf(), packed });
        return Ok(());
    }
    let parent = physical.parent().ok_or_else(|| invalid("wildcard mount has no parent"))?;
    if !parent.is_dir() { return Ok(()); }
    let virtual_leaf = virtual_path.rsplit('/').next().unwrap_or(virtual_path);
    if !virtual_leaf.contains(['*', '?']) {
        return Err(invalid("wildcard physical mount requires a wildcard virtual path"));
    }
    if !virtual_leaf.eq_ignore_ascii_case(leaf) {
        return Err(invalid("wildcard mount with different virtual/physical patterns is not supported"));
    }
    let prefix = virtual_path.strip_suffix(virtual_leaf).unwrap();
    let mut names = BTreeMap::new();
    for entry in std::fs::read_dir(parent)? {
        let entry = entry?;
        let name = entry.file_name().to_string_lossy().to_string();
        let candidate = if packed && name.to_ascii_lowercase().ends_with(".pak") { &name[..name.len() - 4] }
            else if entry.file_type()?.is_dir() { &name } else { continue; };
        if wildcard(leaf, candidate) { names.entry(candidate.to_ascii_lowercase()).or_insert_with(|| candidate.to_string()); }
    }
    for name in names.into_values() {
        out.push(Mount { virtual_path: format!("{prefix}{name}"), physical: parent.join(name), packed });
    }
    Ok(())
}

fn collect_loose(base: &Path, dir: &Path, prefix: &str, out: &mut BTreeMap<String, VfsEntry>) -> Result<(), VfsError> {
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let path = entry.path();
        if kind.is_symlink() { continue; }
        if kind.is_dir() {
            collect_loose(base, &path, prefix, out)?;
        } else if kind.is_file() {
            let relative = normalize(&path.strip_prefix(base).map_err(|e| invalid(e.to_string()))?.to_string_lossy())?;
            let virtual_path = format!("{prefix}/{relative}");
            out.insert(virtual_path.to_ascii_lowercase(), VfsEntry {
                path: virtual_path, size: entry.metadata()?.len(), source: Source::Loose(path.clone()), loose: path,
            });
        }
    }
    Ok(())
}
