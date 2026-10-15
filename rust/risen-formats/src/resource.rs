//! Compiled mesh resources: `._xmsh` (data\common\meshes.pak, magic "GR01MS02", eCMeshResource2) and
//! `._xmac` (data\compiled\animations.pak, "GR01MA02", eCMotionActorResource2). Header = magic, u32 offset
//! of the resource object, ...; the object is a property object with INLINE strings (u16 length + bytes)
//! instead of string table indices. Only its `Boundary` property is read: the mesh's local bounding
//! box, which world entities store as their local box (exactly, for ~25000 of ~26600 eCMesh_PS entities).

use crate::bin_io::{Reader, Result};

pub(crate) fn inline_str(r: &mut Reader) -> Result<String> {
    let n = r.u16()? as usize;
    Ok(String::from_utf8_lossy(r.bytes(n)?).into_owned())
}

#[derive(Debug, Clone)]
pub struct InlineProperty {
    pub name: String,
    pub ty: String,
    pub value: Vec<u8>,
}

/// The common resource object header. `data_offset` follows its property list, before class data.
pub fn properties(data: &[u8]) -> Result<(String, Vec<InlineProperty>, usize)> {
    let mut head = Reader::new(data);
    let magic = head.bytes(8)?;
    if !matches!(magic, b"GR01MS02" | b"GR01MA02" | b"GR01CM00") { return head.err("unsupported resource magic"); }
    let at = head.u32()? as usize;
    let mut r = Reader::new(data.get(at..).ok_or_else(|| crate::bin_io::ParseError { offset: at, msg: "resource offset outside file".into() })?);
    r.u16()?;
    if r.u8()? != 1 { return r.err("empty resource object"); }
    r.bytes(3)?;
    let class = inline_str(&mut r)?;
    r.bytes(11)?;
    let props = inline_properties(&mut r)?;
    Ok((class, props, at + r.pos()))
}

pub(crate) fn inline_properties(r: &mut Reader) -> Result<Vec<InlineProperty>> {
    r.u16()?;
    let count = r.u32()? as usize;
    if count > r.remaining() / 12 { return r.err("invalid inline property count"); }
    let mut props = Vec::with_capacity(count);
    for _ in 0..count {
        let name = inline_str(r)?;
        let ty = inline_str(r)?;
        r.u16()?;
        let size = r.u32()? as usize;
        props.push(InlineProperty { name, ty, value: r.bytes(size)?.to_vec() });
    }
    Ok(props)
}

/// `Boundary` (bCBox: min xyz, max xyz) of a compiled mesh / motion actor; `None` when the file
/// is something else or has no such property.
pub fn boundary(data: &[u8]) -> Option<[f32; 6]> {
    if data.len() < 12 || !matches!(&data[..8], b"GR01MS02" | b"GR01MA02") {
        return None;
    }
    let at = u32::from_le_bytes(data[8..12].try_into().ok()?) as usize;
    let mut r = Reader::new(data.get(at..)?);
    let find = |r: &mut Reader| -> Result<Option<[f32; 6]>> {
        r.u16()?; // version
        if r.u8()? == 0 {
            return Ok(None);
        }
        r.bytes(3)?;
        let class = inline_str(r)?;
        if class != "eCMeshResource2" && class != "eCMotionActorResource2" {
            return Ok(None);
        }
        r.bytes(3 + 2 + 2 + 4)?; // u16, u8, class version, u16, size
        r.u16()?; // properties version
        for _ in 0..r.u32()? {
            let (name, ty) = (inline_str(r)?, inline_str(r)?);
            r.u16()?;
            let size = r.u32()? as usize;
            if name == "Boundary" && ty == "bCBox" && size == 24 {
                return Ok(Some(r.f32s()?));
            }
            r.bytes(size)?;
        }
        Ok(None)
    };
    find(&mut r).ok().flatten()
}

/// Name of a mesh / animation resource as stored in a pak (`ext` = "_xmsh" / "_xmac"): the property values
/// name the source file or just the stem ("It_Plant_Health_02.xmsh", "It_Wpn_Axe_Ogerhammer",
/// "#g3:/data/raw/meshes/objects//x._xmsh", "It_Wpn_Bow_Hunting.xac"), the paks hold the compiled one
/// ("It_Plant_Health_02._xmsh", "It_Wpn_Bow_Hunting._xmac").
pub fn compiled_name(file: &str, ext: &str) -> String {
    let base = file.rsplit(['/', '\\']).next().unwrap_or(file);
    let stem = match base.rsplit_once('.') {
        Some((stem, e)) if ["xmsh", "_xmsh", "xac", "_xmac", "xcom", "_xcom", "xnvmsh", "spt", "_xspt"].iter().any(|x| x.eq_ignore_ascii_case(e)) => stem,
        _ => base,
    };
    format!("{stem}.{ext}")
}
