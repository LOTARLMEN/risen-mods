//! Genome property objects (`bCAccessorPropertyObject`): a class name, a list of named, typed
//! properties, and class-specific binary data.
//!
//! ```text
//! object     = u16 version, u8 valid, [valid: u16, u8, str class, u16, u8, u16 class version, u16,
//!              u32 size of (properties + class data), properties, class data]
//! properties = u16 version, u32 count, count x (str name, str type, u16 version, u32 size, value)
//! ```
//! Property values and unknown class data are kept as raw bytes, so everything round-trips exactly.
//! Class data of the classes we need (layer, entity context) is parsed into structs.

use crate::archive::StrRef;
use crate::bin_io::{ParseError, Reader, Result, Writer};
use crate::lrent::{DynamicContext, DynamicLayer};

#[derive(Debug, Clone)]
pub struct Property {
    pub name: StrRef,
    pub ty: StrRef,
    pub version: u16,
    pub value: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct Properties {
    pub version: u16,
    pub list: Vec<Property>,
}

#[derive(Debug, Clone)]
pub enum ClassData {
    Raw(Vec<u8>),
    DynamicLayer(Box<DynamicLayer>),
    DynamicContext(Box<DynamicContext>),
}

#[derive(Debug, Clone)]
pub struct ObjectBody {
    pub unknown1: u16,
    pub unknown2: u8,
    pub class: StrRef,
    pub unknown3: u16,
    pub unknown4: u8,
    pub class_version: u16,
    pub unknown5: u16,
    pub props: Properties,
    pub data: ClassData,
}

#[derive(Debug, Clone)]
pub struct PropertyObject {
    pub version: u16,
    /// `None` = the "valid" byte was 0 (empty object).
    pub body: Option<ObjectBody>,
}

impl Properties {
    pub fn read(r: &mut Reader) -> Result<Properties> {
        let version = r.u16()?;
        let count = r.u32()?;
        let mut list = Vec::with_capacity(count.min(4096) as usize);
        for _ in 0..count {
            let name = StrRef::read(r)?;
            let ty = StrRef::read(r)?;
            let version = r.u16()?;
            let size = r.u32()? as usize;
            list.push(Property { name, ty, version, value: r.bytes(size)?.to_vec() });
        }
        Ok(Properties { version, list })
    }

    pub fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        w.u32(self.list.len() as u32);
        for p in &self.list {
            p.name.write(w);
            p.ty.write(w);
            w.u16(p.version);
            w.u32(p.value.len() as u32);
            w.bytes(&p.value);
        }
    }

    pub fn get<'a>(&'a self, strings: &[String], name: &str) -> Option<&'a Property> {
        self.list.iter().find(|p| strings.get(p.name.0 as usize).is_some_and(|s| s == name))
    }
}

impl PropertyObject {
    pub fn read(r: &mut Reader, strings: &[String]) -> Result<PropertyObject> {
        let version = r.u16()?;
        if r.u8()? == 0 {
            return Ok(PropertyObject { version, body: None });
        }
        let unknown1 = r.u16()?;
        let unknown2 = r.u8()?;
        let class = StrRef::read(r)?;
        let unknown3 = r.u16()?;
        let unknown4 = r.u8()?;
        let class_version = r.u16()?;
        let unknown5 = r.u16()?;
        let size = r.u32()? as usize;
        let start = r.pos();
        let all = r.bytes(size)?;
        let mut sub = Reader::new(all);
        let props = Properties::read(&mut sub)?;
        let raw = &all[sub.pos()..];
        let class_name = strings.get(class.0 as usize).map(String::as_str).unwrap_or("");
        let data = parse_class(class_name, raw, strings)
            .map_err(|e| ParseError { offset: start + sub.pos() + e.offset, msg: format!("{class_name}: {}", e.msg) })?;
        Ok(PropertyObject {
            version,
            body: Some(ObjectBody {
                unknown1,
                unknown2,
                class,
                unknown3,
                unknown4,
                class_version,
                unknown5,
                props,
                data,
            }),
        })
    }

    pub fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        let Some(b) = &self.body else {
            w.u8(0);
            return;
        };
        w.u8(1);
        w.u16(b.unknown1);
        w.u8(b.unknown2);
        b.class.write(w);
        w.u16(b.unknown3);
        w.u8(b.unknown4);
        w.u16(b.class_version);
        w.u16(b.unknown5);
        w.sized(|w| {
            b.props.write(w);
            match &b.data {
                ClassData::Raw(d) => w.bytes(d),
                ClassData::DynamicLayer(l) => l.write(w),
                ClassData::DynamicContext(c) => c.write(w),
            }
        });
    }

    /// Replaces every occurrence of the 16-byte GUID `from` with `to` in the property values and
    /// raw class data (GUIDs are random, so a false match is practically impossible).
    /// Returns the number of replacements.
    pub fn replace_guid(&mut self, from: &[u8; 16], to: &[u8; 16]) -> usize {
        let Some(b) = self.body.as_mut() else { return 0 };
        let mut n = 0;
        for p in &mut b.props.list {
            n += replace_in(&mut p.value, from, to);
        }
        match &mut b.data {
            ClassData::Raw(d) => n += replace_in(d, from, to),
            ClassData::DynamicLayer(_) | ClassData::DynamicContext(_) => {}
        }
        n
    }

    pub fn class<'a>(&self, strings: &'a [String]) -> &'a str {
        self.body.as_ref().and_then(|b| strings.get(b.class.0 as usize)).map_or("", |s| s.as_str())
    }
}

/// Class data must be consumed exactly, otherwise our reading of the class is wrong.
fn parse_class(name: &str, raw: &[u8], strings: &[String]) -> Result<ClassData> {
    let mut r = Reader::new(raw);
    let data = match name {
        "gCDynamicLayer" => ClassData::DynamicLayer(Box::new(DynamicLayer::read(&mut r, strings)?)),
        "eCEntityDynamicContext" => ClassData::DynamicContext(Box::new(DynamicContext::read(&mut r, strings)?)),
        _ => return Ok(ClassData::Raw(raw.to_vec())),
    };
    if r.remaining() != 0 {
        return r.err(format!("{} bytes of class data not read", r.remaining()));
    }
    Ok(data)
}

/// Human-readable property value for the common types; other types as a byte count.
pub fn format_value(ty: &str, v: &[u8], strings: &[String]) -> String {
    let mut r = Reader::new(v);
    let s = |i: u16| strings.get(i as usize).cloned().unwrap_or_else(|| format!("<str {i}>"));
    let out = match ty {
        "int" | "long" if v.len() == 4 => r.i32().map(|x| x.to_string()),
        "short" if v.len() == 2 => r.u16().map(|x| (x as i16).to_string()),
        "float" if v.len() == 4 => r.f32().map(|x| x.to_string()),
        "bool" | "char" if v.len() == 1 => r.u8().map(|x| x.to_string()),
        "bCString" | "bCMeshResourceString" | "bCImageResourceString" | "bCSpeedTreeResourceString"
            if v.len() == 2 =>
        {
            r.u16().map(|i| format!("{:?}", s(i)))
        }
        "bCVector" if v.len() == 12 => r.f32s::<3>().map(|x| format!("{x:?}")),
        "bCGuid" if v.len() == 20 => Ok(format_guid(&v[..16], v[16] != 0)),
        "eCEntityProxy" | "eCTemplateEntityProxy" if v.len() >= 3 => {
            Ok(if v[2] != 0 && v.len() >= 19 { format_guid(&v[3..19], true) } else { "none".into() })
        }
        t if t.starts_with("bTPropertyContainer<enum ") && v.len() == 6 => r.u16().and(r.u32()).map(|x| {
            format!("{} ({})", x, &t["bTPropertyContainer<enum ".len()..t.len() - 1])
        }),
        _ => Ok(format!("<{} bytes>", v.len())),
    };
    out.unwrap_or_else(|_| format!("<{} bytes>", v.len()))
}

/// Windows GUID text form (first three groups little endian).
pub fn format_guid(b: &[u8], valid: bool) -> String {
    if !valid {
        return "none".into();
    }
    format!(
        "{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
        u32::from_le_bytes(b[0..4].try_into().unwrap()),
        u16::from_le_bytes(b[4..6].try_into().unwrap()),
        u16::from_le_bytes(b[6..8].try_into().unwrap()),
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
    )
}

fn replace_in(buf: &mut [u8], from: &[u8; 16], to: &[u8; 16]) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i + 16 <= buf.len() {
        if buf[i..i + 16] == from[..] {
            buf[i..i + 16].copy_from_slice(to);
            n += 1;
            i += 16;
        } else {
            i += 1;
        }
    }
    n
}
