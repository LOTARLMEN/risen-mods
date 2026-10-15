//! Every string index (u16 into the archive's string table) of a file: visiting them is what moving
//! data between files needs (remap the indices to the target table).
//!
//! Sites: entity / template names, property set classes, object classes, property names and types,
//! property values of string types, class data. A value type or class data we can't read yet is
//! reported as [`Unknown`] instead of guessed, so a caller can refuse to move it.

use std::fmt;

use crate::archive::StrRef;
use crate::bin_io::{ParseError, Reader};
use crate::lrent::{DynamicContext, DynamicEntity, Lrent};
use crate::props::{ClassData, Properties, PropertyObject};
use crate::tple::Tple;

#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Unknown {
    /// Property value type with unknown layout (may hold string indices).
    Value(String),
    /// Value of a known type that didn't read as expected.
    ValueParse(String, String),
    /// Non-empty class data of a class we don't parse.
    ClassData(String),
    /// Class data of a known class that didn't read as expected.
    ClassDataParse(String, String),
}

impl fmt::Display for Unknown {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Unknown::Value(t) => write!(f, "value {t}"),
            Unknown::ValueParse(t, why) => write!(f, "value {t}: {why}"),
            Unknown::ClassData(c) => write!(f, "class data {c}"),
            Unknown::ClassDataParse(c, why) => write!(f, "class data {c}: {why}"),
        }
    }
}

/// Walks the string indices of one file. `f` sees (and may change) every index; type and class
/// names are looked up in `strings` before `f` is called on them, so `f` may remap to another table.
pub struct StrWalk<'a> {
    strings: &'a [String],
    f: &'a mut dyn FnMut(&mut StrRef),
    pub unknown: Vec<Unknown>,
}

/// Property value types with no string indices whose size we don't check (only top level).
fn plain_any_size(ty: &str) -> bool {
    matches!(
        ty,
        "bCRange1"
            | "bCRange2"
            | "bCRange3"
            | "bCRange4"
            | "bCEulerAngles"
            | "bCFloatColor"
            | "bCFloatAlphaColor"
            | "bCByteColor"
            | "bCByteAlphaColor"
            | "bTPropertyContainer<bool>"
    )
}

/// Size of a fixed-size value type without string indices.
fn plain_size(ty: &str) -> Option<usize> {
    Some(match ty {
        "bool" | "char" => 1,
        "short" => 2,
        "int" | "long" | "float" => 4,
        "bCVector2" => 8,
        "bCVector" => 12,
        "bCVector4" | "bCQuaternion" => 16,
        "bCGuid" => 20,
        "bCBox" => 24,
        "bCMotion" => 28, // position + quaternion
        "bCMatrix" => 64,
        "gSVisitedNavCluster" => 16,                          // int + vector
        "gSTrailElement" => 33,                               // bCMotion + int + byte
        t if t.starts_with("bTPropertyContainer<enum ") => 6, // u16 version + u32 value
        _ => return None,
    })
}

fn is_string_type(ty: &str) -> bool {
    matches!(
        ty,
        "bCString"
            | "bCImageOrMaterialResourceString"
            | "bCImageResourceString"
            | "bCSpeedTreeResourceString"
            | "bCMeshResourceString"
            | "eCLocString"
            | "gCLetterLocString"
            | "gCBookLocString"
            | "gCInfoLocString"
            | "gCQuestLocString"
            | "eCTipLocString"
            | "gCNPCInfoLocString"
            | "eCGuiBitmapProxy2" // image name, 2 bytes like bCString
    )
}

/// Key size of a `bTInterpolatorLinear` / `bTInterpolatorBezier` value: time + 1 (linear, plus
/// 2 bytes) or 5 (bezier) values of the key type.
fn interpolator_key_size(ty: &str) -> Option<usize> {
    let value = if ty.ends_with("<class bCVector> >") {
        12
    } else if ty.ends_with("<class bCQuaternion> >") {
        16
    } else {
        return None;
    };
    if ty.contains("bSKeyFrameLinearEx<") {
        Some(4 + value + 2)
    } else if ty.contains("bSKeyFrameBezier<") {
        Some(4 + 5 * value)
    } else {
        None
    }
}

/// Types serialized as `bCObjectBase`: u16 version, u32 size, properties, class data.
fn is_object_base(ty: &str) -> bool {
    matches!(
        ty,
        "gCInteractionSlot" | "gCSkillValue" | "gCModifySkill" | "gCSkillRange" | "gCCraftIngredient" | "gCMapEntry"
            | "gCFlightPathSeeking" | "gCFlightPathBallistic"
            // gCInventory_PS class data: stacks and slots (our names, never a property type)
            | "gCInventoryStack" | "gCInventorySlot"
    )
}

type Sites = std::result::Result<(), String>;

/// Collects the offsets of the string indices of one value of type `ty` read from `r`.
/// Layouts from the Risen Editor (GENOME_Base.cs getObject and the value classes).
fn value_sites(strings: &[String], ty: &str, r: &mut Reader, out: &mut Vec<usize>) -> Sites {
    let e = |e: crate::bin_io::ParseError| e.msg;
    if let Some(n) = plain_size(ty) {
        r.bytes(n).map_err(e)?;
    } else if is_string_type(ty) {
        out.push(r.pos());
        r.u16().map_err(e)?;
    } else if matches!(ty, "eCEntityProxy" | "eCTemplateEntityProxy") {
        r.u16().map_err(e)?;
        if r.u8().map_err(e)? != 0 {
            r.bytes(20).map_err(e)?; // bCPropertyID
        }
    } else if matches!(
        ty,
        "eCScriptProxyScript" | "gCScriptProxyAIState" | "gCScriptProxyAIFunction" | "eCEntityStringProxy"
    ) {
        r.u16().map_err(e)?;
        if r.u8().map_err(e)? != 0 {
            out.push(r.pos());
            r.u16().map_err(e)?;
        }
    } else if ty == "gSRoutine" {
        r.u16().map_err(e)?; // version
        value_sites(strings, "bCString", r, out)?;
        for _ in 0..3 {
            value_sites(strings, "eCEntityProxy", r, out)?;
        }
    } else if ty == "gCWaterSubZone_PS" {
        let v = r.u16().map_err(e)?;
        if v < 2 {
            return Err(format!("version {v}"));
        }
        r.bytes(12).map_err(e)?;
        array_sites(strings, "bTValArray<class bCVector>", r, out)?;
        r.bytes(4 + 12 + 16).map_err(e)?;
    } else if is_object_base(ty) {
        r.u16().map_err(e)?;
        let size = r.u32().map_err(e)? as usize;
        let start = r.pos();
        let all = r.bytes(size).map_err(e)?;
        let mut sub = Reader::new(all);
        let mut inner = Vec::new();
        props_sites(strings, &mut sub, &mut inner)?;
        // Class data = the object's own u16 version (bCObjectBase), or nothing.
        if !matches!(sub.remaining(), 0 | 2) {
            return Err(format!("{} bytes of class data", sub.remaining()));
        }
        out.extend(inner.into_iter().map(|o| start + o));
    } else if ty.starts_with("bTInterpolator") {
        // Mover keys: u8 1, u32 count, keys of floats (time, value(s), linear: 2 flag bytes)
        let key = interpolator_key_size(ty).ok_or_else(|| format!("unknown type {ty}"))?;
        r.u8().map_err(e)?;
        let n = r.u32().map_err(e)? as usize;
        r.bytes(n * key).map_err(e)?;
    } else if ty.starts_with("bTObjArray<") || ty.starts_with("bTValArray<") {
        array_sites(strings, ty, r, out)?;
    } else {
        return Err(format!("unknown type {ty}"));
    }
    Ok(())
}

/// `bTObjArray<T>` / `bTValArray<T>`: u8 1, u32 count, elements (u8 0 or an empty value = empty array).
fn array_sites(strings: &[String], ty: &str, r: &mut Reader, out: &mut Vec<usize>) -> Sites {
    let e = |e: crate::bin_io::ParseError| e.msg;
    let inner = &ty[ty.find('<').unwrap() + 1..ty.len() - 1];
    let (elem, is_enum) = match inner.split_once(' ') {
        Some(("enum", t)) => (t, true),
        Some(("class" | "struct", t)) => (t, false),
        _ => (inner, false),
    };
    let elem = elem.rsplit("::").next().unwrap_or(elem);
    if r.remaining() == 0 {
        return Ok(());
    }
    if r.u8().map_err(e)? == 0 {
        return Ok(()); // empty
    }
    let count = r.u32().map_err(e)?;
    for _ in 0..count {
        if is_enum {
            r.u32().map_err(e)?;
        } else {
            value_sites(strings, elem, r, out)?;
        }
    }
    Ok(())
}

/// Property list: u16 version, u32 count, (str name, str type, u16 version, u32 size, value).
fn props_sites(strings: &[String], r: &mut Reader, out: &mut Vec<usize>) -> Sites {
    let e = |e: crate::bin_io::ParseError| e.msg;
    r.u16().map_err(e)?;
    let count = r.u32().map_err(e)?;
    for _ in 0..count {
        out.push(r.pos());
        r.u16().map_err(e)?;
        out.push(r.pos());
        let ty = strings.get(r.u16().map_err(e)? as usize).map_or("", |s| s.as_str());
        r.u16().map_err(e)?;
        let size = r.u32().map_err(e)? as usize;
        let start = r.pos();
        let v = r.bytes(size).map_err(e)?;
        let mut inner = Vec::new();
        whole_value_sites(strings, ty, v, &mut inner)?;
        out.extend(inner.into_iter().map(|o| start + o));
    }
    Ok(())
}

/// Sites of a complete value buffer (must be consumed exactly).
fn whole_value_sites(strings: &[String], ty: &str, v: &[u8], out: &mut Vec<usize>) -> Sites {
    if plain_any_size(ty) {
        return Ok(());
    }
    // Pointer arrays (state graph event filters / states): u32 count, accessor objects; or no bytes.
    if ty.starts_with("bTRefPropertyArray<") || ty.starts_with("bTRefPtrArray<") {
        if v.is_empty() {
            return Ok(());
        }
        let mut r = Reader::new(v);
        objects_sites(strings, &mut r, out)?;
        return if r.remaining() == 0 { Ok(()) } else { Err(format!("{} bytes left", r.remaining())) };
    }
    let mut r = Reader::new(v);
    value_sites(strings, ty, &mut r, out)?;
    if r.remaining() != 0 {
        return Err(format!("{} bytes left", r.remaining()));
    }
    Ok(())
}

/// Classes whose class data is only a u16 version (checked: exactly 2 bytes).
const VERSION_ONLY: &[&str] = &[
    "eCArea_StringProperty_PS",
    "eCAudioEmitter_PS",
    "eCBillboard_PS",
    "eCDecal_PS",
    "eCDirectionalLight_PS",
    "eCHemisphere_PS",
    "eCPointLight_PS",
    "eCPrecipitation_PS",
    "eCSkydome_PS",
    "eCStrip_PS",
    "eCWeatherZone_PS",
    "eCPhysicsScene_PS",
    "gCAIHelper_FreePoint_PS",
    "gCAIHelper_Label_PS",
    "gCAIZone_PS",
    "gCArena_PS",
    "gCBook_PS",
    "gCCastInfo_PS",
    "gCCharacterMovement_PS",
    "gCClock_PS",
    "gCDamage_PS",
    "gCDialog_PS",
    "gCDoor_PS",
    "gCDynamicCollisionCircle_PS",
    "gCEffect_PS",
    "gCFocus_PS",
    "gCGameScript_PS",
    "gCLetter_PS",
    "gCMapInfo_PS",
    "gCNavHelper_PS",
    "gCNavigation_PS",
    "gCNavOffset_PS",
    "gCNPC_PS",
    "gCRecipe_PS",
    "gCScriptRoutine_PS",
    "gCWaterZone_PS",
    "gCStateGraphEventFilterDamageAmount",
    "gCStateGraphEventFilterIsPlayer",
    "gCStateGraphEventFilterIsNPC",
    "gCStateGraphEventFilterScript",
    "gCInteraction",
    "eCMoverAnimationSkeletal",
    "eCMoverAnimationFile",
    "eCMoverAnimationLinear",
    "eCMoverAnimationBezier",
];

fn str_name(strings: &[String], i: u16) -> &str {
    strings.get(i as usize).map_or("", |s| s.as_str())
}

/// Value of an enum property (`bTPropertyContainer<enum ...>` = u16 version, u32 value).
fn prop_enum(strings: &[String], props: &Properties, name: &str) -> Option<u32> {
    let p = props.get(strings, name)?;
    (p.value.len() == 6).then(|| u32::from_le_bytes(p.value[2..6].try_into().unwrap()))
}

/// `bCAccessorPropertyObject` inside class data: same layout as [`PropertyObject`].
fn object_sites(strings: &[String], r: &mut Reader, out: &mut Vec<usize>) -> Sites {
    let e = |e: ParseError| e.msg;
    r.u16().map_err(e)?;
    if r.u8().map_err(e)? == 0 {
        return Ok(());
    }
    r.bytes(3).map_err(e)?;
    out.push(r.pos());
    let class = str_name(strings, r.u16().map_err(e)?);
    r.bytes(7).map_err(e)?;
    let size = r.u32().map_err(e)? as usize;
    let start = r.pos();
    let all = r.bytes(size).map_err(e)?;
    let props = Properties::read(&mut Reader::new(all)).map_err(e)?;
    let mut sub = Reader::new(all);
    let mut inner = Vec::new();
    props_sites(strings, &mut sub, &mut inner)?;
    let at = sub.pos();
    let mut data = Vec::new();
    class_sites(strings, class, &props, &all[at..], &mut data).map_err(|why| format!("{class}: {why}"))?;
    out.extend(inner.into_iter().map(|o| start + o));
    out.extend(data.into_iter().map(|o| start + at + o));
    Ok(())
}

/// u32 count, accessor objects.
fn objects_sites(strings: &[String], r: &mut Reader, out: &mut Vec<usize>) -> Sites {
    let n = r.u32().map_err(|e| e.msg)?;
    for _ in 0..n {
        object_sites(strings, r, out)?;
    }
    Ok(())
}

/// `eCEntityPropertySet` reference: u16 version, [u8 if version > 1].
fn entity_set(r: &mut Reader) -> Sites {
    let e = |e: ParseError| e.msg;
    if r.u16().map_err(e)? > 1 {
        r.u8().map_err(e)?;
    }
    Ok(())
}

/// eCParticle_PS curves: i32 version, i32 has-data, [u32 count, count x `elem` bytes].
fn particle_scale(r: &mut Reader, elem: usize) -> Sites {
    let e = |e: ParseError| e.msg;
    r.u32().map_err(e)?;
    if r.u32().map_err(e)? != 0 {
        let n = r.u32().map_err(e)? as usize;
        r.bytes(n * elem).map_err(e)?;
    }
    Ok(())
}

/// Class data of an object of class `class` with properties `props`; must be consumed exactly.
/// Layouts from the Risen Editor (LRENT_Base.cs), sizes confirmed on every file of the game.
/// Level-only classes (portal rooms, occluders, vegetation, sector persistence, ...) are left
/// unknown on purpose: they are never moved between layers.
fn class_sites(strings: &[String], class: &str, props: &Properties, d: &[u8], out: &mut Vec<usize>) -> Sites {
    if d.is_empty() {
        return Ok(());
    }
    let e = |e: ParseError| e.msg;
    let mut r = Reader::new(d);
    let r = &mut r;
    match class {
        c if VERSION_ONLY.contains(&c) => {
            r.u16().map_err(e)?;
        }
        "eCAnimation_PS" | "eCBodyPart_PS" => entity_set(r)?,
        "eCSpeedTree_PS" | "eCSpeedTreeWind_PS" => {
            r.u16().map_err(e)?;
            entity_set(r)?;
        }
        "eCMesh_PS" => {
            if r.u16().map_err(e)? == 2 {
                r.bytes(24).map_err(e)?; // bCBox
            }
            entity_set(r)?;
        }
        "eCStaticAmbientLight_PS" | "eCStaticPointLight_PS" => {
            if r.u16().map_err(e)? > 1 {
                r.bytes(12).map_err(e)?;
            }
        }
        // u16 version, u64 date
        "gCCollisionCircle_PS" | "gCNavPath_PS" | "gCNavZone_PS" | "gCNegZone_PS" | "gCPrefPath_PS" => {
            r.bytes(10).map_err(e)?;
        }
        "gCCombatSystem_PS" => {
            r.bytes(3).map_err(e)?;
        }
        "gCItem_PS" => {
            r.bytes(7).map_err(e)?;
        }
        "gCLock_PS" => {
            r.bytes(2 + 8 * 4).map_err(e)?;
        }
        "gCSkills_PS" => {
            r.bytes(2 + 136).map_err(e)?;
        }
        "eCRigidBody_PS" => {
            let v = r.u16().map_err(e)?;
            let n = [(40, 2), (41, 1), (51, 24), (55, 56), (65, 1)].iter().filter(|(min, _)| v >= *min).map(|(_, n)| n);
            r.bytes(n.sum()).map_err(e)?;
        }
        // Floats only; v12 = u16, u8, 8 x vec4, u8, 7 x vec4, float (older versions are shorter).
        "eCIlluminated_PS" => {
            let v = r.u16().map_err(e)?;
            let size = match v {
                8 => 115,
                10 => 244,
                12 => 248,
                _ => return Err(format!("version {v}")),
            };
            r.bytes(size - 2).map_err(e)?;
        }
        "gCAnchor_PS" | "gCMap_PS" => {
            let v = r.u16().map_err(e)?;
            if v == 1 {
                return Err("version 1 layout not supported".into());
            }
        }
        "gCParty_PS" => match r.u16().map_err(e)? {
            1 => {
                value_sites(strings, "bCString", r, out)?;
                value_sites(strings, "eCEntityProxy", r, out)?;
            }
            2 => {}
            _ => {
                value_sites(strings, "bCString", r, out)?;
                r.u8().map_err(e)?;
                value_sites(strings, "eCEntityProxy", r, out)?;
            }
        },
        "eCParticle_PS" => {
            let v = r.u16().map_err(e)?;
            if v == 2 || v == 3 {
                particle_scale(r, 8)?;
                particle_scale(r, 16)?;
                particle_scale(r, 16)?;
                particle_scale(r, 20)?;
                particle_scale(r, 16)?;
                if v == 2 {
                    r.u32().map_err(e)?;
                    let n = r.u32().map_err(e)? as usize;
                    r.bytes(n).map_err(e)?;
                }
            }
        }
        // Inventory stacks and slots are bCObjectBase like gCSkillValue.
        "gCInventory_PS" => {
            r.bytes(5).map_err(e)?;
            let n = r.u32().map_err(e)?;
            for _ in 0..n {
                value_sites(strings, "gCInventoryStack", r, out)?;
            }
            // Slots: version 2 = u8 has-slots, [u32 count, objects]; else u32 count, (i32 slot, object).
            if r.u16().map_err(e)? == 2 {
                if r.u8().map_err(e)? != 0 {
                    let n = r.u32().map_err(e)?;
                    for _ in 0..n {
                        value_sites(strings, "gCInventorySlot", r, out)?;
                    }
                }
            } else {
                let n = r.u32().map_err(e)?;
                for _ in 0..n {
                    r.u32().map_err(e)?;
                    value_sites(strings, "gCInventorySlot", r, out)?;
                }
            }
        }
        // State graph actions (gCStateGraphAction*): u16 version, event filter objects.
        c if c == "gCInteraction_PS" || c == "gCStateGraph_PS" || c.starts_with("gCStateGraphAction") => {
            r.u16().map_err(e)?;
            objects_sites(strings, r, out)?;
        }
        "gCStateGraphState" => {
            r.u16().map_err(e)?;
            r.bytes(16).map_err(e)?; // bCRect editor layout
            objects_sites(strings, r, out)?;
            let n = r.u32().map_err(e)?;
            for _ in 0..n {
                r.u16().map_err(e)?; // transition: u16 version, bCString
                value_sites(strings, "bCString", r, out)?;
            }
        }
        "eCMover_PS" => {
            let v = r.u16().map_err(e)?;
            objects_sites(strings, r, out)?;
            if v > 1 {
                r.bytes(64).map_err(e)?; // matrix
                entity_set(r)?;
            }
        }
        "eCCollisionShape_PS" => {
            let v = r.u16().map_err(e)?;
            objects_sites(strings, r, out)?;
            if (41..=62).contains(&v) {
                objects_sites(strings, r, out)?;
                if v == 62 {
                    r.u8().map_err(e)?;
                }
            }
        }
        "eCCollisionShape" => {
            let v = r.u16().map_err(e)?;
            let shape = if v < 28 { 1 } else { prop_enum(strings, props, "ShapeType").ok_or("no ShapeType")? };
            match shape {
                1 | 6 => {
                    value_sites(strings, "bCMeshResourceString", r, out)?; // tri mesh / convex hull
                    if v > 31 {
                        r.u16().map_err(e)?; // resource index
                    }
                }
                3 => {
                    r.bytes(12 + 12 + 36).map_err(e)?; // oriented box: center, extent, 3x3 matrix
                }
                4 => {
                    r.bytes(if v < 39 { 8 } else { 4 + 4 + 36 + 12 }).map_err(e)?; // capsule
                }
                5 => {
                    r.bytes(16).map_err(e)?; // sphere
                }
                7 => {
                    r.bytes(12).map_err(e)?; // point
                }
                0 | 2 => {}
                s => return Err(format!("shape type {s}")),
            }
            let mut n = 0;
            if v >= 33 {
                n += 2; // bCObjectRefBase
            }
            if v >= 34 {
                n += 24; // outer box
            }
            if v >= 61 {
                n += 12; // last position
            }
            if v < 74 && shape == 3 {
                n += 36;
            }
            r.bytes(n).map_err(e)?;
        }
        _ => return Err(format!("unknown class {class}")),
    }
    if r.remaining() != 0 {
        return Err(format!("{} bytes left", r.remaining()));
    }
    Ok(())
}

impl<'a> StrWalk<'a> {
    pub fn new(strings: &'a [String], f: &'a mut dyn FnMut(&mut StrRef)) -> Self {
        StrWalk { strings, f, unknown: Vec::new() }
    }

    fn name(&self, s: StrRef) -> &'a str {
        self.strings.get(s.0 as usize).map_or("", |s| s.as_str())
    }

    pub fn str(&mut self, s: &mut StrRef) {
        (self.f)(s);
    }

    /// The u16 index at `buf[at..at + 2]`.
    fn str_at(&mut self, buf: &mut [u8], at: usize) {
        let mut s = StrRef(u16::from_le_bytes([buf[at], buf[at + 1]]));
        (self.f)(&mut s);
        buf[at..at + 2].copy_from_slice(&s.0.to_le_bytes());
    }

    /// Sites are collected first, so a value that doesn't read is left untouched.
    fn value(&mut self, ty: &str, v: &mut [u8]) {
        let mut sites = Vec::new();
        match whole_value_sites(self.strings, ty, v, &mut sites) {
            Ok(()) => {
                for at in sites {
                    self.str_at(v, at);
                }
            }
            Err(why) if why == format!("unknown type {ty}") => self.unknown.push(Unknown::Value(ty.to_string())),
            Err(why) => self.unknown.push(Unknown::ValueParse(ty.to_string(), why)),
        }
    }

    pub fn object(&mut self, o: &mut PropertyObject) {
        let Some(b) = o.body.as_mut() else { return };
        let class = self.name(b.class);
        self.str(&mut b.class);
        for p in &mut b.props.list {
            let ty = self.name(p.ty);
            self.str(&mut p.name);
            self.str(&mut p.ty);
            self.value(ty, &mut p.value);
        }
        match &mut b.data {
            ClassData::Raw(d) => {
                let mut sites = Vec::new();
                match class_sites(self.strings, class, &b.props, d, &mut sites) {
                    Ok(()) => {
                        for at in sites {
                            self.str_at(d, at);
                        }
                    }
                    Err(why) if why == format!("unknown class {class}") => {
                        self.unknown.push(Unknown::ClassData(class.to_string()))
                    }
                    Err(why) => self.unknown.push(Unknown::ClassDataParse(class.to_string(), why)),
                }
            }
            ClassData::DynamicLayer(l) => self.object(&mut l.context),
            ClassData::DynamicContext(c) => self.context(c),
            ClassData::Sector(s) => {
                for layer in &mut s.layers { self.str(layer); }
            }
            ClassData::World(w) => {
                if let Some(s) = &mut w.world_data_file { self.str(s); }
            }
        }
    }

    pub fn entity(&mut self, e: &mut DynamicEntity) {
        self.str(&mut e.name);
        for ps in &mut e.property_sets {
            self.object(&mut ps.object);
        }
    }

    pub fn context(&mut self, c: &mut DynamicContext) {
        for e in &mut c.entities {
            self.entity(e);
        }
    }
}

impl Lrent {
    /// Calls `f` on every string index of the layer; returns what couldn't be walked.
    pub fn walk_strs(&mut self, f: &mut dyn FnMut(&mut StrRef)) -> Vec<Unknown> {
        let mut w = StrWalk::new(&self.archive.strings, f);
        w.object(&mut self.root);
        w.unknown
    }
}

/// Remaps string indices of a source file to a target table: a source string the target doesn't
/// have is appended when first seen, so a table in first-use order stays so.
pub(crate) struct Remap<'a> {
    src: &'a [String],
    /// Source index -> target index, filled as indices are met.
    map: Vec<Option<u16>>,
    index: std::collections::HashMap<String, u16>,
    strings: Vec<String>,
    bad: Option<String>,
}

impl<'a> Remap<'a> {
    pub(crate) fn new(target: &[String], src: &'a [String]) -> Self {
        let mut index = std::collections::HashMap::new();
        for (k, s) in target.iter().enumerate() {
            index.entry(s.clone()).or_insert(k as u16);
        }
        Remap { src, map: vec![None; src.len()], index, strings: target.to_vec(), bad: None }
    }

    pub(crate) fn remap(&mut self, s: &mut StrRef) {
        let k = s.0 as usize;
        let Some(text) = self.src.get(k) else {
            self.bad = Some(format!("string index {k} out of range"));
            return;
        };
        let (index, strings) = (&mut self.index, &mut self.strings);
        s.0 = *self.map[k].get_or_insert_with(|| {
            *index.entry(text.clone()).or_insert_with(|| {
                strings.push(text.clone());
                (strings.len() - 1) as u16
            })
        });
    }

    /// The new target table.
    pub(crate) fn finish(self) -> std::result::Result<Vec<String>, String> {
        if let Some(why) = self.bad {
            return Err(why);
        }
        if self.strings.len() > 1 << 16 {
            return Err("target string table would exceed 65536 entries".into());
        }
        Ok(self.strings)
    }
}

/// One entity group copied by [`Lrent::import`].
#[derive(Debug, Clone)]
pub struct Imported {
    /// Index of the copy (group root) in the target layer.
    pub index: usize,
    /// (source GUID, copy GUID) of every copied entity, in subtree order (root first).
    pub guids: Vec<([u8; 16], [u8; 16])>,
    /// Source-layer entities outside the group that the copy references (GUID bytes found in its
    /// property sets): the copy still points to those originals.
    pub outside_refs: Vec<[u8; 16]>,
}

impl Lrent {
    /// Ok when entity `i` (a direct child of the layer root) with its descendants can be copied to
    /// another layer: every string index in it can be walked.
    pub fn check_movable(&self, i: usize) -> std::result::Result<(), String> {
        let c = self.context().ok_or("layer has no entity context")?;
        if i == 0 || i >= c.entities.len() {
            return Err(format!("entity index {i} out of range 1..{}", c.entities.len()));
        }
        if c.parent(i) != Some(0) {
            return Err(format!("entity {i} is not a direct child of the layer root (not supported yet)"));
        }
        let mut unknown = Vec::new();
        let mut keep = |_: &mut StrRef| {};
        for j in c.subtree(i) {
            let mut e = c.entities[j].clone();
            let mut w = StrWalk::new(self.strings(), &mut keep);
            w.entity(&mut e);
            unknown.extend(w.unknown);
        }
        if unknown.is_empty() {
            return Ok(());
        }
        unknown.sort();
        unknown.dedup();
        let list: Vec<String> = unknown.iter().map(|u| u.to_string()).collect();
        Err(format!("entity {i} can't be moved between layers, unreadable: {}", list.join("; ")))
    }

    /// Copies entities `roots` of `src` (direct children of its root, with their descendants) to the
    /// end of this layer at the same places: new GUIDs, string indices remapped to this table (missing
    /// strings appended in first-use order, so a table in first-use order stays so), root bounds
    /// refreshed. Nothing is changed when any of them can't be moved.
    pub fn import(&mut self, src: &Lrent, roots: &[usize]) -> std::result::Result<Vec<Imported>, String> {
        for &i in roots {
            src.check_movable(i)?;
        }
        let sc = src.context().ok_or("source layer has no entity context")?;
        let src_strings = src.strings();
        let src_ids: std::collections::HashMap<[u8; 16], usize> =
            sc.entities.iter().enumerate().map(|(j, e)| (e.id.guid, j)).collect();
        let mut rm = Remap::new(self.strings(), src_strings);
        let mut groups = Vec::new();
        for &i in roots {
            let tree = sc.subtree(i);
            let mut group: Vec<DynamicEntity> = tree.iter().map(|&j| sc.entities[j].clone()).collect();
            let mut remap = |s: &mut StrRef| rm.remap(s);
            for e in &mut group {
                StrWalk::new(src_strings, &mut remap).entity(e);
            }
            let mut outside_refs = Vec::new();
            for e in &group {
                let mut w = crate::bin_io::Writer::default();
                e.write(&mut w);
                for win in w.buf.windows(16) {
                    let g: [u8; 16] = win.try_into().unwrap();
                    if src_ids.get(&g).is_some_and(|j| !tree.contains(j)) && !outside_refs.contains(&g) {
                        outside_refs.push(g);
                    }
                }
            }
            let guids = crate::lrent::renew_guids(&mut group);
            groups.push((group, crate::lrent::group_pairs(sc, &tree), guids, outside_refs));
        }
        self.archive.strings = rm.finish()?;
        let c = self.context_mut().ok_or("target layer has no entity context")?;
        let mut out = Vec::new();
        for (group, pairs, guids, outside_refs) in groups {
            out.push(Imported { index: c.append_group(group, &pairs), guids, outside_refs });
        }
        c.update_root_bounds();
        Ok(out)
    }
}

/// Entity bytes with every string index zeroed, plus the strings at those sites in walk order:
/// equal for two entities = the same entity, whatever their string tables.
pub fn resolved_entity(strings: &[String], e: &DynamicEntity) -> (Vec<u8>, Vec<String>, Vec<Unknown>) {
    let mut e = e.clone();
    let mut seq = Vec::new();
    let mut f = |s: &mut StrRef| {
        seq.push(strings.get(s.0 as usize).cloned().unwrap_or_else(|| format!("<str {}>", s.0)));
        s.0 = 0;
    };
    let mut w = StrWalk::new(strings, &mut f);
    w.entity(&mut e);
    let unknown = w.unknown;
    let mut out = crate::bin_io::Writer::default();
    e.write(&mut out);
    (out.buf, seq, unknown)
}

impl Tple {
    /// Calls `f` on every string index of the template file; returns what couldn't be walked.
    pub fn walk_strs(&mut self, f: &mut dyn FnMut(&mut StrRef)) -> Vec<Unknown> {
        let mut w = StrWalk::new(&self.archive.strings, f);
        for e in &mut self.entities {
            w.str(&mut e.name);
            for s in &mut e.sets {
                w.str(&mut s.class);
                w.object(&mut s.object);
            }
        }
        w.unknown
    }
}
