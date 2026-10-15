//! World/sector project files (`.wrl` / `.sec`). Both hold an eCProcessibleElement:
//! u32 0xD0DEFADE + property object inside a GENOMFLE archive, like a dynamic layer.
//! A sector lists layer basenames (without .lrent); a world points to a text .wrldatasc
//! whose [Sector.List] selects sectors. Layouts checked against Game.dll Rev. 2045;
//! RisenEditor's sector parser wrongly gates the v200 layer list on the enabled byte.

use crate::archive::{Archive, StrRef};
use crate::bin_io::{Reader, Result, Writer};
use crate::props::{ClassData, PropertyObject};

pub const BODY_MAGIC: u32 = 0xD0DE_FADE;

#[derive(Debug, Clone)]
pub struct Sector {
    pub version: u16,
    /// Present from v27. Independent of the v200 layer list; preserve the actual byte.
    pub enabled: Option<u8>,
    pub reserved: Option<u32>,
    pub layers: Vec<StrRef>,
}

impl Sector {
    pub(crate) fn read(r: &mut Reader) -> Result<Self> {
        let version = r.u16()?;
        let enabled = if version >= 27 { Some(r.u8()?) } else { None };
        let mut layers = Vec::new();
        let reserved = if version >= 200 {
            let count = r.u32()? as usize;
            let reserved = r.u32()?;
            if count > r.remaining() / 2 {
                return r.err(format!("sector layer count {count} exceeds remaining data"));
            }
            for _ in 0..count {
                layers.push(StrRef::read(r)?);
            }
            Some(reserved)
        } else { None };
        Ok(Self { version, enabled, reserved, layers })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        if let Some(enabled) = self.enabled {
            w.u8(enabled);
        }
        if self.version >= 200 {
            w.u32(self.layers.len() as u32);
            w.u32(self.reserved.unwrap_or(0));
            for s in &self.layers { s.write(w); }
        }
    }
}

#[derive(Debug, Clone)]
pub struct World {
    pub version: u16,
    /// Present from v36; points to the sector list, not directly to any .sec file.
    pub world_data_file: Option<StrRef>,
}

impl World {
    pub(crate) fn read(r: &mut Reader) -> Result<Self> {
        let version = r.u16()?;
        let world_data_file = if version >= 36 { Some(StrRef::read(r)?) } else { None };
        Ok(Self { version, world_data_file })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        if let Some(s) = self.world_data_file { s.write(w); }
    }
}

/// Typed project object and its original string table. Unknown properties stay lossless.
#[derive(Debug, Clone)]
pub struct ProjectFile {
    pub archive: Archive,
    pub root: PropertyObject,
}

impl ProjectFile {
    pub fn read(data: &[u8]) -> Result<Self> {
        let mut archive = Archive::read(data)?;
        let body = std::mem::take(&mut archive.body);
        let mut r = Reader::new(&body);
        if r.u32()? != BODY_MAGIC { return r.err("bad project body magic"); }
        let root = PropertyObject::read(&mut r, &archive.strings)?;
        let refs: Vec<StrRef> = match root.body.as_ref().map(|b| &b.data) {
            Some(ClassData::Sector(s)) => s.layers.clone(),
            Some(ClassData::World(w)) => w.world_data_file.into_iter().collect(),
            _ => return r.err("expected a gCSector or gCWorld object"),
        };
        if refs.iter().any(|s| s.0 as usize >= archive.strings.len()) {
            return r.err("project reference outside string table");
        }
        if r.remaining() != 0 { return r.err(format!("{} bytes after the project object", r.remaining())); }
        Ok(Self { archive, root })
    }

    pub fn write(&self) -> Vec<u8> {
        let mut w = Writer::default();
        w.u32(BODY_MAGIC);
        self.root.write(&mut w);
        Archive { body: w.buf, ..self.archive.clone() }.write()
    }

    pub fn sector(&self) -> Option<&Sector> {
        match &self.root.body.as_ref()?.data {
            ClassData::Sector(s) => Some(s),
            _ => None,
        }
    }

    pub fn world(&self) -> Option<&World> {
        match &self.root.body.as_ref()?.data {
            ClassData::World(w) => Some(w),
            _ => None,
        }
    }

    /// New enabled, persistent sector using the installed game's header/property versions.
    /// The donor's old layer names and unused strings are discarded.
    pub fn sector_like(&self, layers: &[&str]) -> std::result::Result<Self, String> {
        use crate::strs::{Remap, StrWalk};
        let sector = self.sector().ok_or("donor is not a sector")?;
        if sector.version < 200 { return Err("sector donor needs version >=200".into()); }
        if layers.len() + self.archive.strings.len() > 65536 {
            return Err("sector string table would exceed 65536 entries".into());
        }
        if layers.iter().any(|s| s.is_empty() || !s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')) {
            return Err("layer names must contain only ASCII letters, digits and underscores".into());
        }
        let mut out = self.clone();
        let refs = layers.iter().map(|s| out.archive.intern(s)).collect();
        let body = out.root.body.as_mut().ok_or("empty donor object")?;
        let ClassData::Sector(s) = &mut body.data else { return Err("donor is not a sector".into()) };
        s.layers = refs;
        s.enabled = Some(1);
        s.reserved = Some(0);
        for p in &mut body.props.list {
            let name = out.archive.str(p.name);
            if matches!(name, "EnabledOnStartup" | "SaveGameRelevant" | "IsPersistable") {
                if out.archive.str(p.ty) != "bool" || p.value.len() != 1 {
                    return Err(format!("unexpected type / size of sector property {name}"));
                }
                p.value[0] = 1;
            }
        }
        let strings = out.archive.strings.clone();
        let mut rm = Remap::new(&[], &strings);
        let mut f = |s: &mut StrRef| rm.remap(s);
        let mut walk = StrWalk::new(&strings, &mut f);
        walk.object(&mut out.root);
        if !walk.unknown.is_empty() { return Err(format!("unreadable sector donor: {:?}", walk.unknown)); }
        out.archive.strings = rm.finish()?;
        Ok(out)
    }
}
