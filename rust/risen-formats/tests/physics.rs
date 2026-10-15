use risen_formats::archive::StrRef;
use risen_formats::bin_io::Writer;
use risen_formats::bounds::{enum_value, floats};
use risen_formats::collision::boolean;
use risen_formats::physics::{set_mode, Mode, FROZEN, KINEMATIC, DISABLE_GRAVITY};
use risen_formats::props::{ClassData, ObjectBody, Properties, Property, PropertyObject};

fn body(flags: u32) -> (PropertyObject, Vec<String>) {
    let strings: Vec<_> = ["eCRigidBody_PS", "BodyFlag", "bTPropertyContainer<enum eERigidbody_Flag>",
        "PhysicsEnabled", "bool", "WakeUpCounter", "float", "StartVelocity", "bCVector"].iter().map(|s| s.to_string()).collect();
    let props = vec![
        Property { name: StrRef(1), ty: StrRef(2), version: 30, value: [7u16.to_le_bytes().to_vec(), flags.to_le_bytes().to_vec()].concat() },
        Property { name: StrRef(3), ty: StrRef(4), version: 30, value: vec![0] },
        Property { name: StrRef(5), ty: StrRef(6), version: 30, value: 0f32.to_le_bytes().to_vec() },
        Property { name: StrRef(7), ty: StrRef(8), version: 30, value: vec![42; 12] },
    ];
    (PropertyObject { version: 1, body: Some(ObjectBody { unknown1: 1, unknown2: 1, class: StrRef(0),
        unknown3: 1, unknown4: 0, class_version: 55, unknown5: 201,
        props: Properties { version: 201, list: props }, data: ClassData::Raw(vec![19; 100]) }) }, strings)
}
fn bytes(o: &PropertyObject) -> Vec<u8> { let mut w = Writer::default(); o.write(&mut w); w.buf }

#[test]
fn dynamic_unlocks_gravity_and_preserves_unknown_bits_pose_and_container_version() {
    let (mut o, strings) = body(0x10000 | FROZEN | KINEMATIC | DISABLE_GRAVITY);
    let original = o.clone();
    assert_eq!(set_mode(&mut o, &strings, Mode::Dynamic).unwrap(), (0x100ff, 0x10000));
    assert_eq!(enum_value(&o, &strings, "BodyFlag"), Some(0x10000));
    assert_eq!(boolean(&o, &strings, "PhysicsEnabled"), Some(true));
    assert_eq!(floats::<1>(&o, &strings, "WakeUpCounter"), Some([1.]));
    // Undo only the three intended property values: the entire object must then be identical.
    for i in 0..3 { o.body.as_mut().unwrap().props.list[i].value = original.body.as_ref().unwrap().props.list[i].value.clone(); }
    assert_eq!(bytes(&o), bytes(&original));
}

#[test]
fn frozen_locks_axes_but_preserves_gravity_policy_and_unknown_flags() {
    let (mut o, strings) = body(0x20000 | KINEMATIC | DISABLE_GRAVITY);
    set_mode(&mut o, &strings, Mode::Frozen).unwrap();
    assert_eq!(enum_value(&o, &strings, "BodyFlag"), Some(0x2007f));
    assert_eq!(boolean(&o, &strings, "PhysicsEnabled"), Some(true));
    assert_eq!(floats::<1>(&o, &strings, "WakeUpCounter"), Some([0.]));
    let once = bytes(&o);
    set_mode(&mut o, &strings, Mode::Frozen).unwrap();
    assert_eq!(bytes(&o), once);
}

#[test]
fn malformed_missing_duplicate_or_wrongly_typed_properties_refuse_atomically() {
    for index in 0..3 {
        for case in 0..4 {
            let (mut o, strings) = body(FROZEN);
            let props = &mut o.body.as_mut().unwrap().props.list;
            match case {
                0 => { props[index].value.pop(); }
                1 => { props.remove(index); }
                2 => { props.push(props[index].clone()); }
                _ => { props[index].ty = StrRef(0); }
            }
            let before = bytes(&o);
            assert!(set_mode(&mut o, &strings, Mode::Dynamic).is_err());
            assert_eq!(bytes(&o), before);
        }
    }
    let (mut o, mut strings) = body(FROZEN);
    strings[0] = "eCMesh_PS".into();
    let before = bytes(&o);
    assert!(set_mode(&mut o, &strings, Mode::Dynamic).is_err());
    assert_eq!(bytes(&o), before);
}
