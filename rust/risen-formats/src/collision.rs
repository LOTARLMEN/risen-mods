//! Collision shape metadata, kept separate from render meshes and broad-phase entity bounds.
use crate::bin_io::{Reader, Result};
use crate::bounds::{enum_value, valid_box, value};
use crate::props::{ClassData, PropertyObject};

#[derive(Debug, Clone)]
pub enum Primitive {
    Box { center: [f32;3], extent: [f32;3], orientation: [f32;9] },
    Sphere { radius: f32, center: [f32;3] },
    Capsule { height: f32, radius: f32, orientation: [f32;9], center: [f32;3] },
}

#[derive(Debug, Clone)]
pub struct Shape {
    pub kind: u32,
    pub version: u16,
    pub mesh: Option<String>,
    pub resource_index: Option<u16>,
    pub outer_box: Option<[f32; 6]>,
    pub primitive: Option<Primitive>,
    pub group: Option<u32>,
    pub adapt_to_aabb: Option<bool>,
    pub adapt_mode: Option<u32>,
    pub disabled: Option<bool>,
    pub response_disabled: Option<bool>,
    pub ignored_by_ray: Option<bool>,
    pub override_entity_aabb: Option<bool>,
}

pub fn boolean(o: &PropertyObject, strings: &[String], name: &str) -> Option<bool> {
    let b = value(o, strings, name)?;
    (b.len() == 1).then(|| b[0] != 0)
}

pub fn checked_boolean(o:&PropertyObject,strings:&[String],name:&str)->Result<Option<bool>> {
    let Some(body)=&o.body else {return Ok(None);};
    let mut found=body.props.list.iter().filter(|p|strings.get(p.name.0 as usize).is_some_and(|s|s==name));
    let Some(p)=found.next() else {return Ok(None);};
    if found.next().is_some() || p.value.len()!=1 || p.value[0]>1 {
        return Reader::new(&[]).err(format!("invalid or duplicate collision flag {name}"));
    }
    Ok(Some(p.value[0]!=0))
}

/// Edit only the serialized ledge-climbing permission of an existing collision PS.
/// Runtime climbing also depends on geometry, clearance and the owner's native policy.
/// Refuse missing/ambiguous/noncanonical properties before changing any bytes.
pub fn set_climbable(o: &mut PropertyObject, strings: &[String], enabled: bool) -> Result<bool> {
    let err = |msg: &str| crate::bin_io::ParseError { offset: 0, msg: msg.into() };
    if o.class(strings) != "eCCollisionShape_PS" { return Err(err("expected eCCollisionShape_PS")); }
    let props = &o.body.as_ref().unwrap().props.list;
    let indices: Vec<_> = props.iter().enumerate().filter(|(_, p)|
        strings.get(p.name.0 as usize).is_some_and(|s| s == "IsClimbable")).collect();
    if indices.len() != 1 { return Err(err("expected exactly one IsClimbable")); }
    let (i, p) = indices[0];
    if !strings.get(p.ty.0 as usize).is_some_and(|s| s == "bool") ||
        p.value.len() != 1 || p.value[0] > 1 {
        return Err(err("unsupported IsClimbable representation"));
    }
    let old = p.value[0] != 0;
    o.body.as_mut().unwrap().props.list[i].value[0] = u8::from(enabled);
    Ok(old)
}

/// Individual shape flags and the parent PS flags both participate in runtime filtering.
pub fn shapes(o: &PropertyObject, strings: &[String]) -> Result<Vec<Shape>> {
    let Some(body) = &o.body else { return Ok(Vec::new()) };
    let ClassData::Raw(raw) = &body.data else { return Reader::new(&[]).err("expected collision class data") };
    let mut r = Reader::new(raw);
    r.u16()?;
    let n = r.u32()? as usize;
    if n > r.remaining() / 3 { return r.err("invalid collision shape count"); }
    let mut out = Vec::with_capacity(n);
    for _ in 0..n {
        let object = PropertyObject::read(&mut r, strings)?;
        let Some(body) = &object.body else { continue };
        if object.class(strings) != "eCCollisionShape" { return r.err("unexpected collision shape class"); }
        let ClassData::Raw(raw) = &body.data else { return r.err("expected raw shape data") };
        let mut s = Reader::new(raw);
        let version = s.u16()?;
        let kind = if version < 28 { 1 } else { enum_value(&object, strings, "ShapeType").ok_or_else(||
            crate::bin_io::ParseError { offset: 0, msg: "missing ShapeType".into() })? };
        let mut mesh = None;
        let mut resource_index = None;
        let mut primitive = None;
        match kind {
            1 | 6 => {
                let index = s.u16()? as usize;
                mesh = Some(strings.get(index).ok_or_else(|| crate::bin_io::ParseError { offset: s.pos(), msg: "invalid collision mesh string".into() })?.clone());
                if version > 31 { resource_index = Some(s.u16()?); }
            }
            3 => { primitive = Some(Primitive::Box { center:s.f32s()?, extent:s.f32s()?, orientation:s.f32s()? }); }
            4 => {
                if version < 39 { s.bytes(8)?; }
                else { primitive = Some(Primitive::Capsule { height:s.f32()?, radius:s.f32()?, orientation:s.f32s()?, center:s.f32s()? }); }
            }
            5 => { primitive = Some(Primitive::Sphere { radius:s.f32()?, center:s.f32s()? }); }
            7 => { s.bytes(12)?; }
            0 | 2 => {}
            _ => return s.err(format!("unsupported collision shape {kind}")),
        }
        if version >= 33 { s.u16()?; }
        let outer_box = if version >= 34 { Some(s.f32s()?).filter(|b| valid_box(*b)) } else { None };
        if version >= 61 { s.bytes(12)?; }
        if version < 74 && kind == 3 { s.bytes(36)?; }
        if s.remaining() != 0 { return s.err("bytes after collision shape"); }
        out.push(Shape { kind, version, mesh, resource_index, outer_box, primitive,
            group: enum_value(&object, strings, "Group"),
            adapt_to_aabb: checked_boolean(&object, strings, "AdaptToAABB")?,
            adapt_mode: enum_value(&object, strings, "ShapeAABBAdaptMode"),
            disabled: checked_boolean(&object, strings, "DisableCollision")?,
            response_disabled: checked_boolean(&object, strings, "DisableResponse")?,
            ignored_by_ray: checked_boolean(&object, strings, "IgnoredByTraceRay")?,
            override_entity_aabb: boolean(&object, strings, "OverrideEntityAABB") });
    }
    // Legacy PS versions can carry a second array; metadata above refers to the primary shapes.
    Ok(out)
}

pub fn shape_name(kind: u32) -> &'static str {
    match kind { 0 => "none", 1 => "triangle_mesh", 2 => "plane", 3 => "box", 4 => "capsule", 5 => "sphere",
        6 => "convex_hull", 7 => "point", _ => "unknown" }
}
