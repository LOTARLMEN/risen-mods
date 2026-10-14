//! Risen `.pak` archives ("G3V0" volumes).
//!
//! Layout (little endian):
//! ```text
//! header (48 bytes)
//!   u32 version (1), "G3V0", u64 0, u32 default compression, u32 0,
//!   u64 data offset (0x30), u64 root directory offset, u64 volume size (= file size)
//! file data ...
//! directory tree at root offset:
//!   dir   = name, 3 x u64 FILETIME, u32 attributes, u32 child count,
//!           child count x (u32 attributes, dir | file)   - the leading attributes pick the kind
//!   file  = name, u64 data offset, 3 x u64 FILETIME, u32 attributes,
//!           u32 encryption, u32 compression, u32 stored size, u32 size
//!   name  = u32 length, then length + 1 bytes incl. the trailing NUL (nothing when length is 0)
//! ```
//! Only the directory is read on open; file data is read on demand, so the 1 GB speech paks are cheap.

use std::fs::File;
use std::io::{self, BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use binrw::BinRead;

pub const ATTR_DIRECTORY: u32 = 0x10;

pub const COMPRESSION_NONE: u32 = 0;
pub const COMPRESSION_AUTO: u32 = 1;
pub const COMPRESSION_ZLIB: u32 = 2;

#[derive(Debug, Clone, BinRead)]
#[br(little, magic = 1u32)]
pub struct Header {
    #[br(assert(id == *b"G3V0", "not a G3V0 pak"))]
    pub id: [u8; 4],
    pub reserved0: u64,
    pub default_compression: u32,
    pub reserved1: u32,
    pub data_offset: u64,
    pub root_offset: u64,
    pub volume_size: u64,
}

#[derive(Debug, Clone, Copy, BinRead)]
#[br(little)]
pub struct Times {
    pub created: u64,
    pub accessed: u64,
    pub modified: u64,
}

#[derive(Debug, Clone)]
pub struct Entry {
    /// Path inside the pak, `\`-separated, as stored (case kept).
    pub path: String,
    pub offset: u64,
    pub times: Times,
    pub attributes: u32,
    pub encryption: u32,
    pub compression: u32,
    pub stored_size: u32,
    pub size: u32,
}

impl Entry {
    /// File name without the directory part.
    pub fn name(&self) -> &str {
        self.path.rsplit('\\').next().unwrap_or(&self.path)
    }
}

pub struct Pak {
    pub path: PathBuf,
    pub header: Header,
    /// Files in directory order (depth first, as stored). Directories are not listed.
    pub entries: Vec<Entry>,
}

#[derive(Debug, thiserror::Error)]
pub enum PakError {
    #[error("{0}")]
    Io(#[from] io::Error),
    #[error("{0}")]
    Parse(#[from] binrw::Error),
    #[error("encrypted entry {0} (encryption {1}) is not supported")]
    Encrypted(String, u32),
    #[error("entry {0}: unknown compression {1}")]
    Compression(String, u32),
    #[error("entry {path}: unpacked {got} bytes, directory says {want}")]
    SizeMismatch { path: String, got: usize, want: u32 },
}

impl Pak {
    pub fn open(path: impl AsRef<Path>) -> Result<Pak, PakError> {
        let path = path.as_ref().to_path_buf();
        let mut r = BufReader::new(File::open(&path)?);
        let header = Header::read(&mut r)?;
        r.seek(SeekFrom::Start(header.root_offset))?;
        let mut entries = Vec::new();
        read_dir(&mut r, "", &mut entries)?;
        Ok(Pak { path, header, entries })
    }

    /// Case-insensitive lookup by file name or by full path inside the pak.
    pub fn find(&self, name: &str) -> Option<&Entry> {
        let has_dir = name.contains(['\\', '/']);
        let name = name.replace('/', "\\");
        self.entries.iter().find(|e| {
            let key = if has_dir { e.path.as_str() } else { e.name() };
            key.eq_ignore_ascii_case(&name)
        })
    }

    /// Reads and unpacks one file.
    pub fn read(&self, e: &Entry) -> Result<Vec<u8>, PakError> {
        if e.encryption != 0 {
            return Err(PakError::Encrypted(e.path.clone(), e.encryption));
        }
        let mut f = File::open(&self.path)?;
        f.seek(SeekFrom::Start(e.offset))?;
        let mut stored = vec![0u8; e.stored_size as usize];
        f.read_exact(&mut stored)?;
        let data = match e.compression {
            COMPRESSION_NONE | COMPRESSION_AUTO => stored,
            COMPRESSION_ZLIB => {
                let mut out = Vec::with_capacity(e.size as usize);
                flate2::read::ZlibDecoder::new(&stored[..]).read_to_end(&mut out)?;
                out
            }
            c => return Err(PakError::Compression(e.path.clone(), c)),
        };
        if data.len() != e.size as usize {
            return Err(PakError::SizeMismatch { path: e.path.clone(), got: data.len(), want: e.size });
        }
        Ok(data)
    }
}

fn u32_le<R: Read>(r: &mut R) -> io::Result<u32> {
    let mut b = [0u8; 4];
    r.read_exact(&mut b)?;
    Ok(u32::from_le_bytes(b))
}

fn u64_le<R: Read>(r: &mut R) -> io::Result<u64> {
    let mut b = [0u8; 8];
    r.read_exact(&mut b)?;
    Ok(u64::from_le_bytes(b))
}

/// Names are bytes of the game's ANSI code page; decode as Latin-1 so no byte is lost.
fn read_name<R: Read>(r: &mut R) -> io::Result<String> {
    let len = u32_le(r)? as usize;
    if len == 0 {
        return Ok(String::new());
    }
    let mut buf = vec![0u8; len + 1];
    r.read_exact(&mut buf)?;
    buf.pop(); // trailing NUL
    Ok(buf.iter().map(|&b| b as char).collect())
}

fn join(dir: &str, name: &str) -> String {
    if dir.is_empty() { name.to_string() } else { format!("{dir}\\{name}") }
}

fn read_dir<R: Read + Seek>(r: &mut R, parent: &str, out: &mut Vec<Entry>) -> Result<(), PakError> {
    let name = read_name(r)?;
    let _times = Times::read(r)?;
    let _attributes: u32 = u32_le(r)?;
    let count: u32 = u32_le(r)?;
    // The root directory's name is empty; a subdirectory's name is its own name only.
    let dir = join(parent, &name);
    for _ in 0..count {
        let attributes: u32 = u32_le(r)?;
        if attributes & ATTR_DIRECTORY != 0 {
            read_dir(r, &dir, out)?;
        } else {
            out.push(read_file(r, &dir)?);
        }
    }
    Ok(())
}

fn read_file<R: Read + Seek>(r: &mut R, dir: &str) -> Result<Entry, PakError> {
    let name = read_name(r)?;
    Ok(Entry {
        path: join(dir, &name),
        offset: u64_le(r)?,
        times: Times::read(r)?,
        attributes: u32_le(r)?,
        encryption: u32_le(r)?,
        compression: u32_le(r)?,
        stored_size: u32_le(r)?,
        size: u32_le(r)?,
    })
}
