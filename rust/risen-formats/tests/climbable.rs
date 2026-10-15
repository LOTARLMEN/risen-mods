use risen_formats::archive::StrRef;
use risen_formats::bin_io::Writer;
use risen_formats::collision::set_climbable;
use risen_formats::props::{ClassData, ObjectBody, Properties, Property, PropertyObject};

fn collider() -> (PropertyObject, Vec<String>) {
    let strings = ["eCCollisionShape_PS", "IsClimbable", "bool", "IgnoredByTraceRay"].map(str::to_string).to_vec();
    let props = vec![
        Property { name: StrRef(1), ty: StrRef(2), version: 30, value: vec![0] },
        Property { name: StrRef(3), ty: StrRef(2), version: 30, value: vec![1] },
    ];
    (PropertyObject { version: 1, body: Some(ObjectBody { unknown1: 1, unknown2: 1, class: StrRef(0),
        unknown3: 1, unknown4: 0, class_version: 64, unknown5: 201,
        props: Properties { version: 201, list: props }, data: ClassData::Raw(vec![17; 128]) }) }, strings)
}
fn bytes(o: &PropertyObject) -> Vec<u8> { let mut w = Writer::default(); o.write(&mut w); w.buf }

#[test]
fn climbable_toggle_changes_one_byte_and_restores_the_entire_object() {
    let (mut o, strings) = collider();
    let original = bytes(&o);
    assert!(!set_climbable(&mut o, &strings, true).unwrap());
    let changed = bytes(&o);
    assert_eq!(original.len(), changed.len());
    assert_eq!(original.iter().zip(&changed).filter(|(a, b)| a != b).count(), 1);
    assert!(set_climbable(&mut o, &strings, true).unwrap());
    assert_eq!(bytes(&o), changed);
    assert!(set_climbable(&mut o, &strings, false).unwrap());
    assert_eq!(bytes(&o), original);
}

#[test]
fn unsupported_climbing_properties_and_wrong_class_refuse_without_mutation() {
    for case in 0..7 {
        let (mut o, mut strings) = collider();
        let props = &mut o.body.as_mut().unwrap().props.list;
        match case {
            0 => { props.remove(0); }
            1 => props.push(props[0].clone()),
            2 => props[0].ty = StrRef(0),
            3 => props[0].value.clear(),
            4 => props[0].value.push(0),
            5 => props[0].value[0] = 2,
            _ => strings[0] = "eCMesh_PS".into(),
        }
        let before = bytes(&o);
        assert!(set_climbable(&mut o, &strings, true).is_err(), "case {case}");
        assert_eq!(bytes(&o), before);
    }
    let mut empty = PropertyObject { version: 1, body: None };
    assert!(set_climbable(&mut empty, &[], true).is_err());
}
