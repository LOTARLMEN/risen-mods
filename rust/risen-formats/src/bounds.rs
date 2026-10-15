//! Template bounds: model resource bounds plus property-set contributions (not collision simulation).
use crate::lrent::{EMPTY_BOX, box_union};
use crate::props::PropertyObject;
use crate::tple::Tple;

pub fn value<'a>(o: &'a PropertyObject, strings: &[String], name: &str) -> Option<&'a [u8]> {
    Some(&o.body.as_ref()?.props.get(strings, name)?.value)
}

pub fn string_value<'a>(o: &PropertyObject, strings: &'a [String], name: &str) -> Option<&'a str> {
    let b = value(o, strings, name)?;
    strings.get(u16::from_le_bytes(b.get(..2)?.try_into().ok()?) as usize).map(String::as_str)
}

pub fn floats<const N: usize>(o: &PropertyObject, strings: &[String], name: &str) -> Option<[f32; N]> {
    let b = value(o, strings, name)?;
    if b.len() != N * 4 { return None; }
    let v = std::array::from_fn(|k| f32::from_le_bytes(b[k * 4..k * 4 + 4].try_into().unwrap()));
    v.iter().all(|x| x.is_finite()).then_some(v)
}

pub fn enum_value(o: &PropertyObject, strings: &[String], name: &str) -> Option<u32> {
    let b = value(o, strings, name)?;
    (b.len() == 6).then(|| u32::from_le_bytes(b[2..6].try_into().unwrap()))
}

pub fn valid_box(b: [f32; 6]) -> bool {
    b.iter().all(|x| x.is_finite()) && (0..3).all(|k| b[k] <= b[3 + k])
}

#[derive(Debug, Clone)]
pub struct Bounds {
    pub local_box: [f32; 6],
    pub sources: Vec<String>,
    pub approximate: bool,
}

/// Loader receives (VFS scope, compiled basename). Resources may be absent, but corrupt/ambiguous
/// mounted resources are errors. Bone/particle/tree runtime bounds remain explicitly approximate.
pub fn template_bounds(t: &Tple, mut load: impl FnMut(&str, &str) -> Result<Option<([f32; 6], String)>, String>)
    -> Result<Bounds, String>
{
    let te = t.template().ok_or("multi-template")?;
    object_bounds(&te.sets.iter().map(|s| &s.object).collect::<Vec<_>>(), t.strings(), &mut load)
}

pub fn object_bounds(objects: &[&PropertyObject], strings: &[String], mut load: impl FnMut(&str, &str) -> Result<Option<([f32; 6], String)>, String>)
    -> Result<Bounds, String>
{
    let mut b = EMPTY_BOX;
    let mut sources = Vec::new();
    let mut approximate = false;
    let visual = objects.iter().any(|o| ["eCMesh_PS", "eCAnimation_PS", "eCSpeedTree_PS", "eCBillboard_PS",
        "eCParticle_PS", "eCDecal_PS", "eCStrip_PS"].contains(&o.class(strings)));
    for o in objects {
        match o.class(strings) {
            "eCMesh_PS" | "eCAnimation_PS" => {
                let animation = o.class(strings) == "eCAnimation_PS";
                let (prop, scope, ext) = if animation {
                    ("ResourceFilePath", "data/compiled/animations", "_xmac")
                } else { ("MeshFileName", "data/common/meshes", "_xmsh") };
                if let Some(file) = string_value(o, strings, prop) {
                    if let Some((part, path)) = load(scope, &crate::resource::compiled_name(file, ext))? {
                        if valid_box(part) { b = box_union(b, part); sources.push(path); }
                    } else { approximate = true; }
                }
                approximate |= animation; // actor pose/body parts can change the resource box
            }
            "eCStrip_PS" => {
                // Engine.dll 0x6fee20: an unpopulated strip merges OffsetLeft/OffsetRight.
                for prop in ["OffsetLeft", "OffsetRight"] {
                    if let Some(v) = floats::<3>(o, strings, prop) {
                        b = box_union(b, [v[0], v[1], v[2], v[0], v[1], v[2]]);
                        sources.push(format!("eCStrip_PS.{prop}"));
                    }
                }
            }
            "eCAudioEmitter_PS" if enum_value(o, strings, "Shape") == Some(1) => {
                if let Some(part) = floats::<6>(o, strings, "BoxShape").filter(|b| valid_box(*b)) {
                    b = box_union(b, part); sources.push("eCAudioEmitter_PS.BoxShape".into());
                }
            }
            "eCDecal_PS" => {
                if let (Some(size), Some(offset)) = (floats::<3>(o, strings, "Size"), floats::<3>(o, strings, "Offset")) {
                    if size.iter().all(|&v| v >= 0.) {
                        let part = std::array::from_fn(|k| offset[k % 3] + if k < 3 { -size[k] / 2. } else { size[k - 3] / 2. });
                        b = box_union(b, part); sources.push("eCDecal_PS.Size/Offset".into());
                    }
                }
            }
            "eCBillboard_PS" => {
                if let (Some(size), Some(offset)) = (floats::<2>(o, strings, "Size"), floats::<3>(o, strings, "Offset")) {
                    let radius = size[0].hypot(size[1]) / 2.;
                    let part = std::array::from_fn(|k| offset[k % 3] + if k < 3 { -radius } else { radius });
                    b = box_union(b, part); sources.push("eCBillboard_PS.Size/Offset".into());
                }
            }
            "eCAudioEmitter_PS" => {
                // Engine.dll 0x4eacf0: non-box audio volume = origin +/- MinDistance * 10.
                if let Some([distance]) = floats::<1>(o, strings, "MinDistance") {
                    let radius = distance * 10.;
                    if radius.is_finite() && radius >= 0. {
                        b = box_union(b, [-radius, -radius, -radius, radius, radius, radius]);
                        sources.push("eCAudioEmitter_PS.MinDistance*10".into());
                    }
                }
            }
            "eCParticle_PS" | "eCSpeedTree_PS" => { approximate = true; }
            "eCCollisionShape_PS" => {
                // Engine.dll 0x652b00: with a visual only explicitly overriding shapes contribute.
                for shape in crate::collision::shapes(o, strings).map_err(|e| e.to_string())? {
                    if !visual || shape.override_entity_aabb == Some(true) {
                        if let Some(part) = shape.outer_box {
                            b = box_union(b, part); sources.push("eCCollisionShape.OuterAABBLocal".into());
                            approximate |= shape.version < 74;
                        }
                    }
                }
            }
            _ => {}
        }
    }
    if !valid_box(b) {
        b = [-50., -50., -50., 50., 50., 50.];
        sources.push("default 1 m cube (unresolved bounds)".into());
        approximate = true;
    }
    Ok(Bounds { local_box: b, sources, approximate })
}
