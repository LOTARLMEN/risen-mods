//! Transform invariants with independent affine fixtures; no installed game required.
use risen_formats::archive::{Archive, StrRef};
use risen_formats::lrent::*;
use risen_formats::props::{ClassData, ObjectBody, Properties, Property, PropertyObject};

fn object(class: u16, data: ClassData, list: Vec<Property>) -> PropertyObject {
    PropertyObject { version: 1, body: Some(ObjectBody {
        unknown1: 1, unknown2: 1, class: StrRef(class), unknown3: 1, unknown4: 0,
        class_version: 200, unknown5: 201, props: Properties { version: 201, list }, data,
    }) }
}

fn entity(matrix: [f32; 16]) -> DynamicEntity {
    let local_box = [-1., -2., -3., 4., 5., 6.];
    let b = transform_box(local_box, &matrix);
    DynamicEntity {
        version: 214, version2: None, id: PropertyId { guid: new_guid(), extra: 0 }, name: StrRef(2),
        local_matrix: matrix, world_box: b, world_sphere: box_sphere(b), local_box,
        creator: Creator::Proxy(EntityProxy { version: 1, id: None }), geo_version: 214, geo_unknown: None,
        world_matrix: matrix, geo_box: b, geo_sphere: box_sphere(b), render_alpha_view_range: Some([1., 1.]),
        geo_unknown16: None, cache_in_range: Some(0.), entity_version: 217, node_version: 210,
        enabled: 1, rendering_enabled: 1, unknown_b1: 0, flags_4000_8000_10000: Some([1; 3]),
        unknown_b2: None, unknown_b3: 1, picking_enabled: 1, collision_enabled: 1, insert_type: 0,
        locked: 0, unknown_b4: None, flag_200: 0, unknown_b5: None, data_changed: 0,
        unknown_b67: None, flag_40000: Some(0), save_game_relevant: Some(1), property_sets: Vec::new(),
    }
}

fn layer() -> Lrent {
    // Nonidentity DRE, rotated/scaled parent, nested child + grandchild, unrelated sibling.
    let root = quat_matrix([0., 0., 0., 1.], [100., 20., -50.]);
    let mut parent = quat_matrix([0., 0.5f32.sqrt(), 0., 0.5f32.sqrt()], [10., 30., 40.]);
    for k in 0..3 { for j in 0..3 { parent[k * 4 + j] *= [2., 3., 4.][k]; } }
    let child = quat_matrix([0., 0., 0., 1.], [7., 8., 9.]);
    let grandchild = quat_matrix([0., 0., 0., 1.], [2., 3., 4.]);
    let locals = [root, parent, child, grandchild, IDENTITY];
    let worlds = [root, matrix_product(&parent, &root), matrix_product(&child, &matrix_product(&parent, &root)),
        matrix_product(&grandchild, &matrix_product(&child, &matrix_product(&parent, &root))), root];
    let entities = locals.into_iter().zip(worlds).map(|(local, world)| {
        let mut e = entity(world); e.local_matrix = local; e
    }).collect();
    let mut context = DynamicContext { version: 212, enabled: Some(1), unknown: None,
        context_box: Some(EMPTY_BOX), entities, hierarchy: vec![(0, 1), (1, 2), (2, 3), (0, 4), (-1, -1)] };
    for i in [2, 1] {
        let b = context.children(i).iter().fold(context.entities[i].world_box, |b, &j| box_union(b, context.entities[j].geo_box));
        context.entities[i].geo_box = b; context.entities[i].geo_sphere = box_sphere(b);
    }
    context.update_root_bounds();
    Lrent { archive: Archive { version: 1, body: Vec::new(), table_version: 1,
        strings: ["gCDynamicLayer", "eCEntityDynamicContext", "Entity", "eCRigidBody_PS", "gCNavigation_PS",
            "LastUseableNavigationPosition", "bCVector", "LastUseableNavigationZoneProxy", "eCEntityProxy"]
            .map(str::to_string).to_vec() }, body_magic: 0xD0DEFADE,
        root: object(0, ClassData::DynamicLayer(Box::new(DynamicLayer { magic: u64::from_le_bytes(*b"GENOMEDL"),
            version: 1, context: object(1, ClassData::DynamicContext(Box::new(context)), Vec::new()) })), Vec::new()) }
}

fn poses(l: &mut Lrent, i: usize) {
    let mut rigid = 65u16.to_le_bytes().to_vec();
    rigid.extend([0; 27 + 56 + 1]); // two motions start at offset 29
    l.context_mut().unwrap().entities[i].property_sets = vec![
        EntityPropertySet { unknown: 1, magic: 0xDEADC0DE, object: object(3, ClassData::Raw(rigid), Vec::new()) },
        EntityPropertySet { unknown: 1, magic: 0xDEADC0DE, object: object(4, ClassData::Raw(vec![0, 0]), vec![
            Property { name: StrRef(5), ty: StrRef(6), version: 30, value: vec![0; 12] },
            Property { name: StrRef(7), ty: StrRef(8), version: 30, value: vec![1, 0, 1, 8, 9] },
        ]) },
    ];
}

fn close(a: &[f32], b: &[f32]) {
    assert_eq!(a.len(), b.len());
    for (a, b) in a.iter().zip(b) { assert!((a - b).abs() < 0.002, "{a} != {b}"); }
}

fn floats(d: &[u8]) -> Vec<f32> {
    d.chunks_exact(4).map(|b| f32::from_le_bytes(b.try_into().unwrap())).collect()
}

fn check_poses(l: &Lrent, i: usize, rotation: bool) {
    let e = &l.entities()[i];
    let body = e.property_sets[0].object.body.as_ref().unwrap();
    let ClassData::Raw(d) = &body.data else { panic!() };
    for at in [29, 57] {
        close(&floats(&d[at..at + 12]), &e.position());
        if rotation {
            let q: [f32; 4] = floats(&d[at + 12..at + 28]).try_into().unwrap();
            let m = quat_matrix(q, [0.; 3]);
            for k in 0..3 {
                let row = &e.world_matrix[k * 4..k * 4 + 3];
                let scale = row.iter().map(|v| v * v).sum::<f32>().sqrt();
                close(&row.iter().map(|v| v / scale).collect::<Vec<_>>(), &m[k * 4..k * 4 + 3]);
            }
        }
    }
    let nav = &e.property_sets[1].object.body.as_ref().unwrap().props.list;
    close(&floats(&nav[0].value), &e.position());
    assert_eq!(nav[1].value, [1, 0, 0]);
}

#[test]
fn nested_world_move_keeps_child_locals_and_unrelated_entities() {
    let mut l = layer();
    poses(&mut l, 2); poses(&mut l, 3);
    let before = l.clone();
    l.translate(2, [40., -15., 20.]).unwrap();
    let c = l.context().unwrap();
    for i in [2, 3] {
        let want: Vec<_> = before.entities()[i].position().into_iter().zip([40., -15., 20.]).map(|(x, d)| x + d).collect();
        close(&c.entities[i].position(), &want);
        close(&matrix_product(&c.entities[i].local_matrix, &c.entities[c.parent(i).unwrap()].world_matrix), &c.entities[i].world_matrix);
        assert_eq!(c.entities[i].local_box, before.entities()[i].local_box);
        close(&c.entities[i].world_box, &transform_box(c.entities[i].local_box, &c.entities[i].world_matrix));
        check_poses(&l, i, false);
    }
    assert_eq!(c.entities[3].local_matrix, before.entities()[3].local_matrix);
    let mut parent = before.entities()[1].clone();
    parent.geo_box = box_union(parent.world_box, c.entities[2].geo_box); parent.geo_sphere = box_sphere(parent.geo_box);
    assert_eq!(format!("{:?}", c.entities[1]), format!("{parent:?}"));
    assert_eq!(format!("{:?}", c.entities[4]), format!("{:?}", before.entities()[4]));
    assert_eq!(c.context_box, Some(c.computed_root_bounds().0));
    let bytes = l.write();
    assert_eq!(Lrent::read(&bytes).unwrap().write(), bytes);
}

#[test]
fn moving_parent_moves_entire_subtree() {
    let mut l = layer(); let before = l.clone();
    l.translate(1, [13., 17., -19.]).unwrap();
    for i in [1, 2, 3] {
        close(&l.entities()[i].position(), &std::array::from_fn::<_, 3, _>(|k| before.entities()[i].position()[k] + [13., 17., -19.][k]));
        if i != 1 { assert_eq!(l.entities()[i].local_matrix, before.entities()[i].local_matrix); }
    }
}

#[test]
fn singular_parent_bad_hierarchy_and_nonfinite_moves_are_atomic() {
    for case in 0..8 {
        let mut l = layer();
        let c = l.context_mut().unwrap();
        match case {
            0 => c.entities[1].world_matrix[0..3].fill(0.),
            1 => c.hierarchy.insert(0, (4, 2)),
            2 => c.hierarchy[0] = (3, 1),
            3 => c.hierarchy[1] = (80, 2),
            4 => { c.hierarchy.remove(1); }
            _ => {}
        }
        let before = l.write();
        let d = match case { 5 => [f32::NAN, 0., 0.], 6 => [f32::INFINITY, 0., 0.], 7 => [1e30, 0., 0.], _ => [1.; 3] };
        assert!(l.translate(2, d).is_err(), "case {case}");
        assert_eq!(l.write(), before);
    }
}

#[test]
fn absolute_yaw_turns_subtree_and_synchronizes_physics() {
    for yaw in [0., 90., 180., 270., -90., 450.] {
        let mut l = layer(); poses(&mut l, 2); poses(&mut l, 3);
        let before = l.clone();
        let pivot = l.entities()[2].position();
        l.set_yaw(2, yaw).unwrap();
        let c = l.context().unwrap();
        close(&c.entities[2].position(), &pivot);
        let m = &c.entities[2].world_matrix;
        let angle = yaw.to_radians();
        close(&[m[8] / 4., m[10] / 4.], &[angle.sin(), angle.cos()]);
        let distance = |l: &Lrent| l.entities()[3].position().iter().zip(pivot).map(|(a, b)| (a - b).powi(2)).sum::<f32>();
        assert!((distance(&l) - distance(&before)).abs() < 0.01);
        assert_eq!(c.entities[3].local_matrix, before.entities()[3].local_matrix);
        for i in [2, 3] {
            close(&matrix_product(&c.entities[i].local_matrix, &c.entities[c.parent(i).unwrap()].world_matrix), &c.entities[i].world_matrix);
            close(&c.entities[i].world_box, &transform_box(c.entities[i].local_box, &c.entities[i].world_matrix));
            check_poses(&l, i, true);
        }
        let mut parent = before.entities()[1].clone();
        parent.geo_box = box_union(parent.world_box, c.entities[2].geo_box); parent.geo_sphere = box_sphere(parent.geo_box);
        assert_eq!(format!("{:?}", c.entities[1]), format!("{parent:?}"));
        assert_eq!(format!("{:?}", c.entities[4]), format!("{:?}", before.entities()[4]));
        let bytes = l.write(); assert_eq!(Lrent::read(&bytes).unwrap().write(), bytes);
    }
}

#[test]
fn yaw_keeps_tilt_roll_and_scale() {
    let mut l = layer();
    let q = [0.3f32, 0.4, 0.2, 0.5];
    let length = q.iter().map(|v| v * v).sum::<f32>().sqrt();
    let mut m = quat_matrix(q.map(|v| v / length), [10., 20., 30.]);
    for k in 0..3 { for j in 0..3 { m[k * 4 + j] *= [2., 3., 4.][k]; } }
    l.context_mut().unwrap().entities[4] = entity(m);
    poses(&mut l, 4);
    for yaw in [0., 90., 180., -90.] {
        l.set_yaw(4, yaw).unwrap();
        let after = &l.entities()[4].world_matrix;
        close(&[after[1], after[5], after[9]], &[m[1], m[5], m[9]]);
        close(&[after[8].atan2(after[10]).sin()], &[yaw.to_radians().sin()]);
        for k in 0..3 { close(&[after[k * 4].hypot(after[k * 4 + 1]).hypot(after[k * 4 + 2])], &[[2., 3., 4.][k]]); }
        check_poses(&l, 4, true);
    }
}

#[test]
fn yaw_errors_are_atomic_and_root_is_refused() {
    for case in 0..5 {
        let mut l = layer(); poses(&mut l, 2);
        match case {
            0 => l.context_mut().unwrap().entities[1].world_matrix[0..3].fill(0.),
            1 => l.context_mut().unwrap().entities[2].world_matrix[4] += 2., // rigid shear
            _ => {}
        }
        let before = l.write();
        assert!(l.set_yaw(if case == 3 { 0 } else { 2 }, if case == 2 { f32::NAN } else if case == 4 { f32::INFINITY } else { 90. }).is_err());
        assert_eq!(l.write(), before);
    }
}

#[test]
fn world_pose_tilts_and_translates_nested_group_preserving_locals_and_syncing_physics() {
    let mut l=layer();poses(&mut l,2);poses(&mut l,3);
    let before=l.clone();
    let mut world=quat_matrix([0.15f32.sin(),0.,0.,0.15f32.cos()],[150.,60.,80.]);
    for r in 0..3 { for k in 0..3 { world[4*r+k]*=[2.,3.,4.][r]; } }
    l.set_world_matrix(2,world).unwrap();
    close(&l.entities()[2].world_matrix,&world);
    assert_eq!(l.entities()[3].local_matrix,before.entities()[3].local_matrix);
    let context=l.context().unwrap();
    for i in [2,3] {
        close(&matrix_product(&l.entities()[i].local_matrix,&l.entities()[context.parent(i).unwrap()].world_matrix),&l.entities()[i].world_matrix);
        close(&l.entities()[i].world_box,&transform_box(l.entities()[i].local_box,&l.entities()[i].world_matrix));
        check_poses(&l,i,true);
    }
    assert_eq!(format!("{:?}",l.entities()[4]),format!("{:?}",before.entities()[4]));
    let bytes=l.write();assert_eq!(Lrent::read(&bytes).unwrap().write(),bytes);
}

#[test]
fn invalid_world_pose_refuses_atomically_and_retains_rounded_parent_matrix() {
    for case in 0..6 {
        let mut l=layer();poses(&mut l,2);poses(&mut l,3);
        let mut world=l.entities()[2].world_matrix;
        match case {
            0=>world[12]=f32::NAN,
            1=>world[0..3].fill(0.),
            2=>world[4]+=0.7,
            3=>world[15]=0.,
            4=>l.context_mut().unwrap().hierarchy.insert(0,(4,2)),
            _=>{},
        }
        let before=l.write();
        assert!(l.set_world_matrix(if case==5 {0} else {2},world).is_err(),"case {case}");
        assert_eq!(l.write(),before);
    }
    let mut l=layer();
    l.context_mut().unwrap().entities[1].world_matrix[15]=0.9999944;
    let parent=l.entities()[1].world_matrix;
    let mut world=l.entities()[2].world_matrix;world[13]+=12.;
    l.set_world_matrix(2,world).unwrap();
    assert_eq!(l.entities()[1].world_matrix,parent);
    close(&matrix_product(&l.entities()[2].local_matrix,&parent),&l.entities()[2].world_matrix);
}

#[test]
fn rounded_homogeneous_parent_element_is_preserved() {
    let mut l = layer();
    l.context_mut().unwrap().entities[1].world_matrix[15] = 0.9999944;
    let c = l.context_mut().unwrap();
    c.entities[2].local_matrix[15] = 1.0000324;
    c.entities[2].world_matrix = matrix_product(&c.entities[2].local_matrix, &c.entities[1].world_matrix);
    c.entities[3].world_matrix = matrix_product(&c.entities[3].local_matrix, &c.entities[2].world_matrix);
    l.translate(2, [10., 20., 30.]).unwrap();
    l.set_yaw(2, 45.).unwrap();
    let e = &l.entities()[2];
    close(&matrix_product(&e.local_matrix, &l.entities()[1].world_matrix), &e.world_matrix);
    assert_eq!(l.entities()[1].world_matrix[15], 0.9999944);
    close(&matrix_product(&l.entities()[3].local_matrix, &e.world_matrix), &l.entities()[3].world_matrix);
}

#[test]
fn inverse_parent_basis_handles_shear_for_translation() {
    let mut l = layer();
    let c = l.context_mut().unwrap();
    c.entities[1].world_matrix[4] += 1.25;
    c.entities[2].world_matrix = matrix_product(&c.entities[2].local_matrix, &c.entities[1].world_matrix);
    c.entities[3].world_matrix = matrix_product(&c.entities[3].local_matrix, &c.entities[2].world_matrix);
    l.translate(2, [17., 31., -23.]).unwrap();
    let e = &l.entities()[2];
    close(&matrix_product(&e.local_matrix, &l.entities()[1].world_matrix), &e.world_matrix);
}

#[test]
fn ancestor_bounds_shrink_after_child_returns() {
    let mut l = layer();
    let before = l.entities()[1].geo_box;
    l.translate(2, [10000., 0., 0.]).unwrap();
    assert!(l.entities()[1].geo_box[3] > before[3]);
    l.translate(2, [-10000., 0., 0.]).unwrap();
    close(&l.entities()[1].geo_box, &before);
}

#[test]
fn authored_extra_bounds_survive_moves_and_rotate_with_the_subtree() {
    let mut l = layer();
    let c = l.context_mut().unwrap();
    c.entities[1].geo_box[0] -= 1000.;
    c.entities[1].geo_sphere = box_sphere(c.entities[1].geo_box);
    let extra_min = c.entities[1].geo_box[0];
    c.entities[3].geo_box[3] += 100.;
    c.entities[3].geo_sphere = box_sphere(c.entities[3].geo_box);
    l.translate(2, [100., 0., 0.]).unwrap();
    assert!(l.entities()[1].geo_box[0] <= extra_min);
    let old = l.entities()[3].geo_box;
    let pivot = l.entities()[2].position();
    let mut turn = quat_matrix([0., -0.5f32.sqrt(), 0., 0.5f32.sqrt()], [0.; 3]);
    let rotated = transform_vector(pivot, &turn);
    for k in 0..3 { turn[12 + k] = pivot[k] - rotated[k]; }
    let expected = transform_box(old, &turn);
    l.set_yaw(2, 0.).unwrap();
    for k in 0..3 {
        assert!(l.entities()[3].geo_box[k] <= expected[k] + 0.002);
        assert!(l.entities()[3].geo_box[3 + k] >= expected[3 + k] - 0.002);
    }
}
