use risen_formats::archive::StrRef;
use risen_formats::bin_io::Writer;
use risen_formats::bounds::object_bounds;
use risen_formats::collision::shapes;
use risen_formats::props::{ClassData, ObjectBody, Properties, Property, PropertyObject};

fn intern(strings: &mut Vec<String>, name: &str) -> StrRef {
    if let Some(i) = strings.iter().position(|s| s == name) { return StrRef(i as u16); }
    strings.push(name.into()); StrRef((strings.len() - 1) as u16)
}
fn object(strings: &mut Vec<String>, class: &str, props: Vec<(&str, Vec<u8>)>, data: Vec<u8>) -> PropertyObject {
    let class = intern(strings, class); let ty = intern(strings, "raw");
    let list = props.into_iter().map(|(name, value)| Property { name: intern(strings, name), ty, version: 30, value }).collect();
    PropertyObject { version: 1, body: Some(ObjectBody { unknown1: 1, unknown2: 1, class, unknown3: 1, unknown4: 0,
        class_version: 74, unknown5: 201, props: Properties { version: 201, list }, data: ClassData::Raw(data) }) }
}
fn floats(v: &[f32]) -> Vec<u8> { v.iter().flat_map(|v| v.to_le_bytes()).collect() }
fn enumeration(v: u32) -> Vec<u8> { [1u16.to_le_bytes().to_vec(), v.to_le_bytes().to_vec()].concat() }
fn collider(strings: &mut Vec<String>, override_bounds: bool) -> PropertyObject {
    let mut raw = Writer::default(); raw.u16(74); raw.bytes(&[0; 60]); raw.u16(1);
    raw.f32s(&[-100., -200., -300., 100., 200., 300.]); raw.f32s(&[0., 0., 0.]);
    let shape = object(strings, "eCCollisionShape", vec![("ShapeType", enumeration(3)), ("OverrideEntityAABB", vec![override_bounds as u8]),
        ("DisableCollision", vec![0]), ("DisableResponse", vec![1])], raw.buf);
    let mut raw = Writer::default(); raw.u16(64); raw.u32(1); shape.write(&mut raw);
    object(strings, "eCCollisionShape_PS", Vec::new(), raw.buf)
}

#[test]
fn visual_bounds_ignore_nonoverriding_colliders_but_include_trails() {
    let mut strings = Vec::new(); let name = intern(&mut strings, "model.xmsh");
    let mesh = object(&mut strings, "eCMesh_PS", vec![("MeshFileName", name.0.to_le_bytes().to_vec())], Vec::new());
    let strip = object(&mut strings, "eCStrip_PS", vec![("OffsetLeft", floats(&[0., 0., 10.])), ("OffsetRight", floats(&[0., 0., 80.]))], Vec::new());
    let collision = collider(&mut strings, false);
    let loader = |_: &str, _: &str| Ok(Some(([-5., -6., -7., 5., 6., 7.], "model".into())));
    let b = object_bounds(&[&mesh, &strip, &collision], &strings, loader).unwrap();
    assert_eq!(b.local_box, [-5., -6., -7., 5., 6., 80.]);
    let overriding = collider(&mut strings, true);
    let b = object_bounds(&[&mesh, &overriding], &strings, loader).unwrap();
    assert_eq!(b.local_box, [-100., -200., -300., 100., 200., 300.]);
    let b = object_bounds(&[&collision], &strings, loader).unwrap();
    assert_eq!(b.local_box, [-100., -200., -300., 100., 200., 300.]);
}

#[test]
fn native_decal_billboard_and_audio_volumes_do_not_use_an_arbitrary_cube() {
    let mut strings = Vec::new();
    let decal = object(&mut strings, "eCDecal_PS", vec![("Size", floats(&[100., 20., 10.])), ("Offset", floats(&[1., 2., 3.]))], Vec::new());
    assert_eq!(object_bounds(&[&decal], &strings, |_, _| Ok(None)).unwrap().local_box, [-49., -8., -2., 51., 12., 8.]);
    let billboard = object(&mut strings, "eCBillboard_PS", vec![("Size", floats(&[6., 8.])), ("Offset", floats(&[1., 2., 3.]))], Vec::new());
    assert_eq!(object_bounds(&[&billboard], &strings, |_, _| Ok(None)).unwrap().local_box, [-4., -3., -2., 6., 7., 8.]);
    let audio = object(&mut strings, "eCAudioEmitter_PS", vec![("Shape", enumeration(0)), ("MinDistance", floats(&[25.]))], Vec::new());
    assert_eq!(object_bounds(&[&audio], &strings, |_, _| Ok(None)).unwrap().local_box, [-250., -250., -250., 250., 250., 250.]);
    let audio = object(&mut strings, "eCAudioEmitter_PS", vec![("Shape", enumeration(1)),
        ("BoxShape", floats(&[-1., -2., -3., 4., 5., 6.])), ("MinDistance", floats(&[25.]))], Vec::new());
    assert_eq!(object_bounds(&[&audio], &strings, |_, _| Ok(None)).unwrap().local_box, [-1., -2., -3., 4., 5., 6.]);
}

#[test]
fn missing_bounds_are_marked_approximate_and_resource_errors_are_not_silenced() {
    let mut strings = Vec::new(); let name = intern(&mut strings, "missing.xmsh");
    let mesh = object(&mut strings, "eCMesh_PS", vec![("MeshFileName", name.0.to_le_bytes().to_vec())], Vec::new());
    let b = object_bounds(&[&mesh], &strings, |_, _| Ok(None)).unwrap();
    assert!(b.approximate); assert_eq!(b.local_box, [-50., -50., -50., 50., 50., 50.]);
    assert!(object_bounds(&[&mesh], &strings, |_, _| Err("ambiguous resource".into())).is_err());
}

#[test]
fn collision_metadata_is_separate_from_render_bounds_and_rejects_truncated_objects() {
    let mut strings = Vec::new(); let o = collider(&mut strings, false);
    let s = shapes(&o, &strings).unwrap();
    assert_eq!(s.len(), 1); assert_eq!(s[0].kind, 3); assert_eq!(s[0].version, 74);
    assert_eq!(s[0].outer_box, Some([-100., -200., -300., 100., 200., 300.]));
    assert_eq!(s[0].override_entity_aabb, Some(false)); assert_eq!(s[0].response_disabled, Some(true));
    let ClassData::Raw(raw) = &o.body.as_ref().unwrap().data else { panic!() };
    for n in 0..raw.len() {
        let mut o = o.clone(); o.body.as_mut().unwrap().data = ClassData::Raw(raw[..n].to_vec());
        assert!(shapes(&o, &strings).is_err(), "prefix {n}");
    }
}
