//! Explicit motion policy for an existing rigid body; does not add physics to static entities.
use crate::bin_io::{ParseError, Result};
use crate::bounds::enum_value;
use crate::props::PropertyObject;

// eERigidbody_Flag (RisenEditor REnums.cs); FROZEN comprises six locked motion axes.
pub const DISABLE_GRAVITY: u32 = 0x01;
pub const FROZEN: u32 = 0x7e;
pub const KINEMATIC: u32 = 0x80;

#[derive(Debug, Clone, Copy)]
pub enum Mode { Dynamic, Frozen }

/// Validate all three properties before editing anything. Unknown flag bits, enum container
/// version, serialized pose, velocities and unrelated properties remain unchanged.
/// Dynamic enables gravity, unlocks all axes and requests an initially awake body. Runtime
/// scripts may still override this policy; in-game simulation is a separate validation step.
pub fn set_mode(o: &mut PropertyObject, strings: &[String], mode: Mode) -> Result<(u32, u32)> {
    let err = |msg: &str| ParseError { offset: 0, msg: msg.into() };
    if o.class(strings) != "eCRigidBody_PS" { return Err(err("expected eCRigidBody_PS")); }
    let old = enum_value(o, strings, "BodyFlag").ok_or_else(|| err("missing or malformed BodyFlag"))?;
    let props = &o.body.as_ref().unwrap().props.list;
    let locate = |name: &str, ty: &str, len: usize| -> Result<usize> {
        let indices: Vec<_> = props.iter().enumerate().filter(|(_, p)| strings.get(p.name.0 as usize).is_some_and(|s| s == name)).collect();
        if indices.len() != 1 { return Err(err(&format!("expected exactly one {name}"))); }
        let (i, p) = indices[0];
        if p.value.len() != len || !strings.get(p.ty.0 as usize).is_some_and(|s| s == ty) {
            return Err(err(&format!("unsupported {name} representation")));
        }
        Ok(i)
    };
    let flags = locate("BodyFlag", "bTPropertyContainer<enum eERigidbody_Flag>", 6)?;
    let enabled = locate("PhysicsEnabled", "bool", 1)?;
    let wake = locate("WakeUpCounter", "float", 4)?;
    let new = match mode {
        Mode::Dynamic => old & !(FROZEN | KINEMATIC | DISABLE_GRAVITY),
        Mode::Frozen => (old & !KINEMATIC) | FROZEN,
    };
    let props = &mut o.body.as_mut().unwrap().props.list;
    props[flags].value[2..6].copy_from_slice(&new.to_le_bytes());
    props[enabled].value[0] = 1;
    props[wake].value.copy_from_slice(&match mode { Mode::Dynamic => 1.0f32, Mode::Frozen => 0.0 }.to_le_bytes());
    Ok((old, new))
}
