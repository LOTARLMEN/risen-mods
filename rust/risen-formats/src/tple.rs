//! Templates (`.tple`, data\common\templates.pak): a GENOMFLE archive whose body is "GENOMETP", u16 version,
//! u32 count, `count` template entities, then (parent, child) hierarchy pairs ending with (-1, -1).
//! Entity 0 is the file root (named like the file, no property sets), entity 1 the template itself
//! (its GUID is what world entities reference as creator). Field layout follows the Risen Editor's
//! TpleFile.cs; unknown version-dependent bytes are kept raw.

use crate::archive::{Archive, StrRef};
use crate::bin_io::{ParseError, Reader, Result, Writer};
use crate::lrent::{PropertyId, read_hierarchy};
use crate::props::PropertyObject;

pub const MAGIC: &[u8; 8] = b"GENOMETP";

/// One property set of a template entity: u16 id, class name, u32 object size, object, u32 0xDEADC0DE.
#[derive(Debug, Clone)]
pub struct TemplateSet {
    pub id: u16,
    pub class: StrRef,
    pub object: PropertyObject,
    pub magic: u32,
}

#[derive(Debug, Clone)]
pub struct TemplateEntity {
    pub version: u16,
    pub id: PropertyId,
    /// Enabled / rendering / picking / ... flag bytes (7 + (version < 215) + (version >= 214)).
    pub flags: Vec<u8>,
    /// Instance of another template (multi-templates): then `sets` is empty and not stored.
    pub ref_template: Option<PropertyId>,
    pub render_alpha: f32,
    pub view_range: f32,
    pub insert_type: u16,
    pub name: StrRef,
    pub position: [f32; 3],
    pub rotation: [f32; 4], // quaternion x y z w
    pub old_a: Vec<u8>,     // version < 213 fields before the date
    pub data_changed: u64,  // FILETIME
    pub old_b: Vec<u8>,     // version < 217 fields after the date
    pub tail: Vec<u8>,      // 3 bytes, scale grid % (>= 211), one byte (>= 218)
    pub sets: Vec<TemplateSet>,
}

fn flag_count(v: u16) -> usize {
    7 + (v < 215) as usize + (v >= 214) as usize
}

fn old_a_len(v: u16) -> usize {
    (if v < 213 { 2 + 4 } else { 0 }) + (v < 212) as usize
}

fn old_b_len(v: u16) -> usize {
    (if v < 213 { 3 } else { 0 }) + (if v < 217 { 4 } else { 0 })
}

fn tail_len(v: u16) -> usize {
    3 + (v >= 211) as usize + (v >= 218) as usize
}

impl TemplateEntity {
    fn read(r: &mut Reader, strings: &[String]) -> Result<Self> {
        let version = r.u16()?;
        let id = PropertyId::read(r)?;
        let flags = r.bytes(flag_count(version))?.to_vec();
        let ref_template = match r.u8()? {
            0 => None,
            1 => Some(PropertyId::read(r)?),
            x => return r.err(format!("ref template flag {x}")),
        };
        let render_alpha = r.f32()?;
        let view_range = r.f32()?;
        let insert_type = r.u16()?;
        let name = StrRef::read(r)?;
        let position = r.f32s()?;
        let rotation = r.f32s()?;
        let old_a = r.bytes(old_a_len(version))?.to_vec();
        let data_changed = r.u64()?;
        let old_b = r.bytes(old_b_len(version))?.to_vec();
        let tail = r.bytes(tail_len(version))?.to_vec();
        // An entity that references another template has no property sets of its own.
        let count = if ref_template.is_some() { 0 } else { r.u32()? };
        let mut sets = Vec::with_capacity(count.min(256) as usize);
        for i in 0..count {
            let set_id = r.u16()?;
            let class = StrRef::read(r)?;
            let size = r.u32()? as usize;
            let start = r.pos();
            let object = PropertyObject::read(r, strings)
                .map_err(|e| ParseError { offset: e.offset, msg: format!("property set {i}/{count}: {}", e.msg) })?;
            if r.pos() - start != size {
                return r.err(format!("property set {i}: object is {} bytes, size field says {size}", r.pos() - start));
            }
            sets.push(TemplateSet { id: set_id, class, object, magic: r.u32()? });
        }
        Ok(TemplateEntity {
            version,
            id,
            flags,
            ref_template,
            render_alpha,
            view_range,
            insert_type,
            name,
            position,
            rotation,
            old_a,
            data_changed,
            old_b,
            tail,
            sets,
        })
    }

    fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        self.id.write(w);
        w.bytes(&self.flags);
        w.u8(self.ref_template.is_some() as u8);
        if let Some(id) = &self.ref_template {
            id.write(w);
        }
        w.f32(self.render_alpha);
        w.f32(self.view_range);
        w.u16(self.insert_type);
        self.name.write(w);
        w.f32s(&self.position);
        w.f32s(&self.rotation);
        w.bytes(&self.old_a);
        w.u64(self.data_changed);
        w.bytes(&self.old_b);
        w.bytes(&self.tail);
        if self.ref_template.is_none() {
            w.u32(self.sets.len() as u32);
        }
        for s in &self.sets {
            w.u16(s.id);
            s.class.write(w);
            w.sized(|w| s.object.write(w));
            w.u32(s.magic);
        }
    }

    /// Class names of the property sets (gCItem_PS, gCNPC_PS, ...).
    pub fn set_classes<'a>(&self, strings: &'a [String]) -> Vec<&'a str> {
        self.sets.iter().map(|s| s.object.class(strings)).collect()
    }
}

/// A whole `.tple` file.
#[derive(Debug, Clone)]
pub struct Tple {
    pub archive: Archive,
    pub version: u16,
    pub entities: Vec<TemplateEntity>,
    pub hierarchy: Vec<(i32, i32)>,
}

impl Tple {
    pub fn read(data: &[u8]) -> Result<Tple> {
        let mut archive = Archive::read(data)?;
        let body = std::mem::take(&mut archive.body);
        let mut r = Reader::new(&body);
        if r.bytes(8)? != MAGIC {
            return r.err("not a GENOMETP template");
        }
        let version = r.u16()?;
        let count = r.u32()?;
        let mut entities = Vec::with_capacity(count.min(1024) as usize);
        for i in 0..count {
            let start = r.pos();
            let e = TemplateEntity::read(&mut r, &archive.strings)
                .map_err(|e| ParseError { offset: e.offset, msg: format!("entity {i}/{count} at 0x{start:X}: {}", e.msg) })?;
            entities.push(e);
        }
        let hierarchy = read_hierarchy(&mut r)?;
        if r.remaining() != 0 {
            return r.err(format!("{} bytes after the hierarchy", r.remaining()));
        }
        Ok(Tple { archive, version, entities, hierarchy })
    }

    pub fn write(&self) -> Vec<u8> {
        let mut w = Writer::default();
        w.bytes(MAGIC);
        w.u16(self.version);
        w.u32(self.entities.len() as u32);
        for e in &self.entities {
            e.write(&mut w);
        }
        for &(p, c) in &self.hierarchy {
            w.i32(p);
            w.i32(c);
        }
        Archive { body: w.buf, ..self.archive.clone() }.write()
    }

    pub fn strings(&self) -> &[String] {
        &self.archive.strings
    }

    /// The template entity (entity 1; entity 0 is the file root). `None` for multi-templates
    /// (editor prefabs: several instances of other templates, entity 1 is such an instance).
    pub fn template(&self) -> Option<&TemplateEntity> {
        self.entities.get(1).filter(|_| !self.is_multi())
    }

    pub fn is_multi(&self) -> bool {
        self.entities.len() != 2 || self.entities.iter().any(|e| e.ref_template.is_some())
    }

    pub fn name(&self, e: &TemplateEntity) -> &str {
        self.archive.str(e.name)
    }
}
