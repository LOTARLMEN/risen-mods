use risen_formats::{
    archive::StrRef,
    bin_io::Writer,
    collider::{self, Surface, VerticalSurface},
    collision::{Primitive, shapes},
    geometry::Mesh,
    lrent::{IDENTITY, quat_matrix},
    placement::ground_plan_on,
    props::{ClassData, ObjectBody, Properties, Property, PropertyObject},
    xcom::{CollisionMesh, CookedMesh, Kind},
};
use std::sync::Arc;

const O: [f32; 9] = [1., 0., 0., 0., 1., 0., 0., 0., 1.];
fn intern(s: &mut Vec<String>, name: &str) -> StrRef {
    if let Some(i) = s.iter().position(|n| n == name) {
        return StrRef(i as u16);
    }
    s.push(name.into());
    StrRef((s.len() - 1) as u16)
}
fn enumeration(n: u32) -> Vec<u8> {
    [1u16.to_le_bytes().to_vec(), n.to_le_bytes().to_vec()].concat()
}
fn object(
    s: &mut Vec<String>,
    class: &str,
    properties: Vec<(&str, Vec<u8>)>,
    raw: Vec<u8>,
) -> PropertyObject {
    let class = intern(s, class);
    let ty = intern(s, "raw");
    let list = properties
        .into_iter()
        .map(|(n, value)| Property {
            name: intern(s, n),
            ty,
            version: 30,
            value,
        })
        .collect();
    PropertyObject {
        version: 1,
        body: Some(ObjectBody {
            unknown1: 1,
            unknown2: 1,
            class,
            unknown3: 1,
            unknown4: 0,
            class_version: 74,
            unknown5: 201,
            props: Properties { version: 201, list },
            data: ClassData::Raw(raw),
        }),
    }
}
fn ps(s: &mut Vec<String>, kind: u32, index: u16, flags: Vec<(&str, Vec<u8>)>) -> PropertyObject {
    let mut raw = Writer::default();
    raw.u16(74);
    match kind {
        1 | 6 => {
            raw.u16(intern(s, "mesh.xnvmsh").0);
            raw.u16(index);
        }
        3 => {
            raw.f32s(&[10., 20., 30.]);
            raw.f32s(&[5., 10., 15.]);
            raw.f32s(&O);
        }
        4 => {
            raw.f32(20.);
            raw.f32(5.);
            raw.f32s(&O);
            raw.f32s(&[10., 20., 30.]);
        }
        5 => {
            raw.f32(5.);
            raw.f32s(&[10., 20., 30.]);
        }
        _ => panic!(),
    }
    raw.u16(1);
    raw.f32s(&[-10., -10., -10., 10., 10., 10.]);
    raw.f32s(&[1000., 2000., 3000.]); // last position is NOT a shape translation
    let mut props = vec![("ShapeType", enumeration(kind)), ("Group", enumeration(1))];
    props.extend(flags);
    let shape = object(s, "eCCollisionShape", props, raw.buf);
    let mut raw = Writer::default();
    raw.u16(64);
    raw.u32(1);
    shape.write(&mut raw);
    object(s, "eCCollisionShape_PS", vec![], raw.buf)
}
fn no_resource(_: &str) -> Result<Arc<CollisionMesh>, String> {
    Err("unexpected resource lookup".into())
}
fn surface(
    o: &PropertyObject,
    s: &[String],
    world: &[f32; 16],
    support: bool,
) -> Result<Vec<Surface>, String> {
    collider::surfaces(&[o], s, world, support, None, no_resource)
}

#[test]
fn primitive_center_half_extents_and_pose_apply_once_without_last_position() {
    let mut s = Vec::new();
    let o = ps(&mut s, 3, 0, vec![]);
    let parsed = shapes(&o, &s).unwrap();
    let Some(Primitive::Box {
        center,
        extent,
        orientation,
    }) = &parsed[0].primitive
    else {
        panic!()
    };
    assert_eq!(*center, [10., 20., 30.]);
    assert_eq!(*extent, [5., 10., 15.]);
    assert_eq!(*orientation, O);
    let mut m = quat_matrix(
        [0., 0.5f32.sqrt(), 0., 0.5f32.sqrt()],
        [1000., 2000., 3000.],
    );
    for r in 0..3 {
        for k in 0..3 {
            m[4 * r + k] *= 2.;
        }
    }
    let c = surface(&o, &s, &m, true).unwrap();
    let b = c[0].bounds().unwrap();
    let expected = [1030., 2020., 2970., 1090., 2060., 2990.];
    for k in 0..6 {
        assert!((b[k] - expected[k]).abs() < 0.001);
    }
    assert!((c[0].surface_y(1060., 2980., 1900., 2100.).unwrap() - 2060.).abs() < 0.001);
}

#[test]
fn analytic_spheres_and_capsules_handle_caps_cylinders_tilt_and_range() {
    let sphere = Surface::Capsule {
        a: [-37000., 100., -6000.],
        b: [-37000., 100., -6000.],
        radius: 10.,
    };
    assert_eq!(sphere.surface_y(-37000., -6000., 0., 200.), Some(110.));
    assert_eq!(sphere.surface_y(-37000., -6000., 0., 105.), Some(90.));
    assert_eq!(sphere.surface_y(-36990., -6000., 0., 200.), Some(100.));
    assert!(sphere.surface_y(-36989., -6000., 0., 200.).is_none());
    let vertical = Surface::Capsule {
        a: [0., 90., 0.],
        b: [0., 110., 0.],
        radius: 5.,
    };
    assert_eq!(vertical.surface_y(0., 0., 0., 200.), Some(115.));
    let horizontal = Surface::Capsule {
        a: [-10., 100., 0.],
        b: [10., 100., 0.],
        radius: 5.,
    };
    assert_eq!(horizontal.surface_y(0., 0., 0., 200.), Some(105.));
    assert_eq!(horizontal.surface_y(13., 0., 0., 200.), Some(104.));
    let tilted = Surface::Capsule {
        a: [-10., 90., 0.],
        b: [10., 110., 0.],
        radius: 5.,
    };
    assert!(
        (tilted.surface_y(0., 0., 0., 200.).unwrap() - (100. + 5. * 2f32.sqrt())).abs() < 0.001
    );
    assert!(tilted.surface_y(0., 0., 200., 300.).is_none());
}

#[test]
fn sphere_and_capsule_serialization_has_the_native_radius_height_and_axis() {
    let mut s = Vec::new();
    for kind in [4, 5] {
        let o = ps(&mut s, kind, 0, vec![]);
        let c = surface(&o, &s, &IDENTITY, true).unwrap();
        assert_eq!(
            c[0].surface_y(10., 30., 0., 100.),
            Some(if kind == 4 { 35. } else { 25. })
        );
    }
}

#[test]
fn shape_and_ps_flags_filter_supports_without_erasing_a_ray_ignored_moving_body() {
    let mut s = Vec::new();
    for name in ["DisableCollision", "DisableResponse"] {
        let o = ps(&mut s, 3, 0, vec![(name, vec![1])]);
        assert!(surface(&o, &s, &IDENTITY, true).unwrap().is_empty());
    }
    let o = ps(&mut s, 3, 0, vec![("IgnoredByTraceRay", vec![1])]);
    assert!(surface(&o, &s, &IDENTITY, true).unwrap().is_empty());
    assert_eq!(surface(&o, &s, &IDENTITY, false).unwrap().len(), 1);
    let mut o = ps(&mut s, 3, 0, vec![]);
    let p = Property {
        name: intern(&mut s, "DisableResponse"),
        ty: intern(&mut s, "bool"),
        version: 30,
        value: vec![1],
    };
    o.body.as_mut().unwrap().props.list.push(p);
    assert!(surface(&o, &s, &IDENTITY, true).unwrap().is_empty());
    let o = ps(&mut s, 3, 0, vec![("DisableCollision", vec![2])]);
    assert!(surface(&o, &s, &IDENTITY, true).is_err());
    let o = ps(
        &mut s,
        3,
        0,
        vec![("DisableCollision", vec![0]), ("DisableCollision", vec![0])],
    );
    assert!(surface(&o, &s, &IDENTITY, true).is_err());
}

fn resource() -> Arc<CollisionMesh> {
    let meshes = [
        (Kind::Triangle, 0, 10.),
        (Kind::Triangle, 1, 50.),
        (Kind::Convex, 0, 80.),
    ]
    .into_iter()
    .map(|(kind, index, y)| {
        let mesh = Mesh {
            vertices: vec![
                [-100., y, -100.],
                [100., y, -100.],
                [100., y, 100.],
                [-100., y, 100.],
            ],
            triangles: vec![[0, 1, 2], [0, 2, 3]],
        };
        let bounds = mesh.bounds().unwrap();
        CookedMesh {
            kind,
            index,
            stream_start: 0,
            stream_end: 0,
            mesh,
            triangle_materials: None,
            bounds,
        }
    })
    .collect();
    Arc::new(CollisionMesh {
        meshes,
        materials: vec![],
        materials_offset: 0,
    })
}

#[test]
fn resource_index_selects_the_correct_kind_and_unknown_relevant_resources_refuse() {
    let mut s = Vec::new();
    for (kind, index, expected) in [(1, 0, 10.), (1, 1, 50.), (6, 0, 80.)] {
        let o = ps(&mut s, kind, index, vec![]);
        let c = collider::surfaces(&[&o], &s, &IDENTITY, true, None, |name| {
            assert_eq!(name, "mesh._xcom");
            Ok(resource())
        })
        .unwrap();
        assert_eq!(c[0].surface_y(0., 0., 0., 100.), Some(expected));
    }
    let o = ps(&mut s, 1, 2, vec![]);
    assert!(collider::surfaces(&[&o], &s, &IDENTITY, true, None, |_| Ok(resource())).is_err());
    let o = ps(&mut s, 1, 0, vec![]);
    assert!(surface(&o, &s, &IDENTITY, true).is_err());
}

#[test]
fn contact_shapes_preserve_native_solid_or_triangle_surface_semantics() {
    let mut s=Vec::new();
    for kind in [1,3,4,5,6] {
        let o=ps(&mut s,kind,0,vec![]);
        let found=collider::collision_shapes(&[&o],&s,&IDENTITY,false,None,|_|Ok(resource())).unwrap();
        assert_eq!(found.len(),1);
        assert_eq!(found[0].solid,kind!=1);
    }
}

#[test]
fn unsupported_native_scale_adaptation_and_mesh_merge_of_curves_refuse() {
    let mut s = Vec::new();
    let o = ps(&mut s, 3, 0, vec![]);
    let mut m = IDENTITY;
    m[0] = 2.;
    assert!(surface(&o, &s, &m, true).is_err());
    let mut m = IDENTITY;
    m[0] = -1.;
    assert!(surface(&o, &s, &m, true).is_err());
    let mut m = IDENTITY;
    m[4] = 0.2;
    assert!(surface(&o, &s, &m, true).is_err());
    let o = ps(&mut s, 3, 0, vec![("AdaptToAABB", vec![1])]);
    assert!(surface(&o, &s, &IDENTITY, true).is_err());
    let o = ps(&mut s, 5, 0, vec![]);
    assert!(collider::merge_meshes(surface(&o, &s, &IDENTITY, false).unwrap()).is_err());
    let mut m = IDENTITY;
    for r in 0..3 {
        m[r * 4 + r] = 2.;
    }
    let o = ps(&mut s, 1, 0, vec![]);
    assert!(collider::surfaces(&[&o], &s, &m, true, None, |_| Ok(resource())).is_err());
    let o = ps(&mut s, 6, 0, vec![]);
    let c = collider::surfaces(&[&o], &s, &m, true, None, |_| Ok(resource())).unwrap();
    assert_eq!(c[0].surface_y(0., 0., 0., 200.), Some(160.));
}

#[test]
fn dense_ground_uses_physical_underside_and_actual_support_height() {
    let body = collider::box_mesh([0., 5., 0.], [10., 5., 10.], O).unwrap();
    let floor = Surface::Mesh(collider::box_mesh([0., 20., 0.], [100., 5., 100.], O).unwrap());
    let mut world = IDENTITY;
    world[13] = 50.;
    let plan =
        ground_plan_on(&body, world, &[floor], None, 100., 100., -2., false, 7, 45.).unwrap();
    assert_eq!(plan.world_matrix[13], 23.);
    assert_eq!(plan.max_clearance, 0.);
    let ball = Surface::Capsule {
        a: [0., 0., 0.],
        b: [0., 0., 0.],
        radius: 100.,
    };
    let plan = ground_plan_on(&body, world, &[ball], None, 100., 100., 0., false, 7, 45.).unwrap();
    assert!((plan.world_matrix[13] - 100.).abs() < 0.001);
}

#[test]
fn query_region_culls_by_transformed_physical_bounds() {
    let mut s = Vec::new();
    let o = ps(&mut s, 1, 1, vec![]);
    let mut m = IDENTITY;
    m[12] = 1000.;
    m[13] = 2000.;
    let region = [900., 2040., -100., 1100., 2060., 100.];
    let c = collider::surfaces(&[&o], &s, &m, true, Some(region), |_| Ok(resource())).unwrap();
    assert_eq!(c.len(), 1);
    assert_eq!(c[0].surface_y(1000., 0., 2000., 2100.), Some(2050.));
    let region = [-100., 0., -100., 100., 100., 100.];
    assert!(
        collider::surfaces(&[&o], &s, &m, true, Some(region), |_| Ok(resource()))
            .unwrap()
            .is_empty()
    );
}
