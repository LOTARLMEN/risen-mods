use risen_formats::{
    collider::{Shape, Surface, box_mesh},
    contact::{Body, query},
    geometry::Mesh,
    lrent::{IDENTITY, quat_matrix},
};
const O: [f32; 9] = [1., 0., 0., 0., 1., 0., 0., 0., 1.];
fn rounded(a: [f32; 3], b: [f32; 3], radius: f32) -> Shape {
    Shape {
        surface: Surface::Capsule { a, b, radius },
        solid: true,
    }
}
fn ball(p: [f32; 3], r: f32) -> Shape {
    rounded(p, p, r)
}
fn polygon(points: Vec<[f32; 3]>, triangles: Vec<[u32; 3]>, solid: bool) -> Shape {
    Shape {
        surface: Surface::Mesh(Mesh {
            vertices: points,
            triangles,
        }),
        solid,
    }
}
fn box_shape(center: [f32; 3], extent: [f32; 3], solid: bool) -> Shape {
    Shape {
        surface: Surface::Mesh(box_mesh(center, extent, O).unwrap()),
        solid,
    }
}
fn body(shapes: &[Shape]) -> Body {
    Body::new(shapes, IDENTITY).unwrap()
}
fn near(a: f64, b: f64) {
    assert!((a - b).abs() < 1e-6, "{a} != {b}");
}

#[test]
fn sphere_capsule_distance_touch_overlap_and_witnesses_are_symmetric() {
    let a = body(&[ball([0.; 3], 2.)]);
    for (x, expected) in [(10., 5.), (5., 0.), (4., 0.), (0., 0.)] {
        let b = body(&[rounded([x, -5., 0.], [x, 5., 0.], 3.)]);
        let q = query(&a, &b);
        let reverse = query(&b, &a);
        near(q.distance_cm, expected);
        near(reverse.distance_cm, expected);
        assert_eq!(q.intersects, expected == 0.);
        let distance = (0..3)
            .map(|k| (q.point_a[k] - q.point_b[k]).powi(2))
            .sum::<f64>()
            .sqrt();
        near(distance, q.distance_cm);
        assert!(q.point_a.iter().map(|v| v * v).sum::<f64>() <= 4. + 1e-7);
        assert!((q.point_b[0] - x as f64).powi(2) + q.point_b[2].powi(2) <= 9. + 1e-7);
    }
    let contained = query(
        &body(&[ball([0.; 3], 10.)]),
        &body(&[ball([1., 0., 0.], 1.)]),
    );
    assert!(contained.intersects);
    assert_eq!(contained.distance_cm, 0.);
}

#[test]
fn capsules_detect_interior_crossings_parallel_segments_and_point_axes() {
    let a = body(&[rounded([-10., 0., 0.], [10., 0., 0.], 0.1)]);
    let b = body(&[rounded([0., -10., 0.], [0., 10., 0.], 0.1)]);
    assert!(query(&a, &b).intersects);
    let parallel = body(&[rounded([-5., 3., 0.], [5., 3., 0.], 0.4)]);
    near(query(&a, &parallel).distance_cm, 2.5);
    let near_parallel = body(&[rounded(
        [-10., -0.00001, 0.001],
        [10., 0.00001, 0.001],
        0.0001,
    )]);
    let thin = body(&[rounded([-10., 0., 0.], [10., 0., 0.], 0.0001)]);
    assert!((query(&thin, &near_parallel).distance_cm - 0.0008).abs() < 1e-9);
}

#[test]
fn capsule_triangle_checks_face_edge_vertex_and_thin_piercing_without_samples() {
    let floor = body(&[polygon(
        vec![[-10., 0., -10.], [10., 0., -10.], [0., 0., 10.]],
        vec![[0, 1, 2]],
        false,
    )]);
    near(
        query(&body(&[ball([0., 5., 0.], 2.)]), &floor).distance_cm,
        3.,
    );
    near(
        query(&body(&[ball([0., 0., 12.], 1.)]), &floor).distance_cm,
        1.,
    );
    near(
        query(&body(&[ball([0., 0., -12.], 1.)]), &floor).distance_cm,
        1.,
    );
    let piercing = body(&[rounded([0., -100., 0.], [0., 100., 0.], 0.001)]);
    assert!(query(&piercing, &floor).intersects);
    let line = body(&[polygon(
        vec![[0., 0., 0.], [10., 0., 0.], [5., 0., 0.]],
        vec![[0, 1, 2]],
        false,
    )]);
    near(
        query(&body(&[ball([5., 3., 0.], 1.)]), &line).distance_cm,
        2.,
    );
}

#[test]
fn triangle_pairs_find_crossings_coplanar_overlap_and_parallel_clearance() {
    let horizontal = polygon(
        vec![[-10., 0., -10.], [10., 0., -10.], [0., 0., 10.]],
        vec![[0, 1, 2]],
        false,
    );
    let vertical = polygon(
        vec![[0., -10., -5.], [0., 10., -5.], [0., 0., 20.]],
        vec![[0, 1, 2]],
        false,
    );
    assert!(query(&body(&[horizontal.clone()]), &body(&[vertical])).intersects);
    let coplanar = polygon(
        vec![[0., 0., -5.], [20., 0., -5.], [10., 0., 5.]],
        vec![[0, 1, 2]],
        false,
    );
    assert!(query(&body(&[horizontal.clone()]), &body(&[coplanar])).intersects);
    let mut shifted = IDENTITY;
    shifted[13] = 0.001;
    let separated = query(
        &body(&[horizontal.clone()]),
        &Body::new(&[horizontal], shifted).unwrap(),
    );
    near(separated.distance_cm, 0.001);
    assert!(!separated.intersects);
}

#[test]
fn solid_containment_differs_from_triangle_shell_containment() {
    let outer = box_shape([0.; 3], [10.; 3], true);
    let inner = box_shape([0.; 3], [1.; 3], true);
    let solid = body(&[outer.clone()]);
    let contained = body(&[inner.clone()]);
    assert!(query(&solid, &contained).intersects);
    assert!(query(&solid, &body(&[ball([0.; 3], 1.)])).intersects);
    let mut shell = outer;
    shell.solid = false;
    near(query(&body(&[shell.clone()]), &contained).distance_cm, 9.);
    near(
        query(&body(&[shell]), &body(&[ball([0.; 3], 1.)])).distance_cm,
        9.,
    );
    let triangle = body(&[polygon(
        vec![[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]],
        vec![[0, 1, 2]],
        false,
    )]);
    assert!(query(&solid, &triangle).intersects);
}

#[test]
fn rotated_boxes_and_compounds_report_shape_pair_and_known_distance() {
    let a = body(&[box_shape([0.; 3], [2., 1., 2.], true)]);
    let mut m = quat_matrix([0., 0.5f32.sqrt(), 0., 0.5f32.sqrt()], [0., 5., 0.]);
    let box_b = box_shape([0.; 3], [2., 1., 2.], true);
    let b = Body::new(&[box_b.clone()], m).unwrap();
    assert!((query(&a, &b).distance_cm - 3.).abs() < 1e-5);
    m[13] = 2.;
    assert!(query(&a, &Body::new(&[box_b], m).unwrap()).distance_cm < 1e-6);
    let compound = body(&[ball([100., 0., 0.], 1.), ball([0., 5., 0.], 1.)]);
    let q = query(&a, &compound);
    near(q.distance_cm, 3.);
    assert_eq!(q.shape_b, 1);
}

#[test]
fn f64_pose_keeps_convex_faces_and_small_clearance_far_from_origin() {
    let local = box_shape([0.; 3], [2., 1., 2.], true);
    let mut a = quat_matrix(
        [0., 0.2f32.sin(), 0., 0.2f32.cos()],
        [-37000., 100., -6000.],
    );
    let mut b = a;
    b[13] = 102.01;
    let q = query(
        &Body::new(&[local.clone()], a).unwrap(),
        &Body::new(&[local.clone()], b).unwrap(),
    );
    near(q.distance_cm, (b[13] - a[13]) as f64 - 2.);
    a[12] = 0.;
    a[14] = 0.;
    b[12] = 0.;
    b[14] = 0.;
    near(
        q.distance_cm,
        query(
            &Body::new(&[local.clone()], a).unwrap(),
            &Body::new(&[local], b).unwrap(),
        )
        .distance_cm,
    );
}

#[test]
fn invalid_poses_shapes_and_open_or_inconsistently_oriented_solids_refuse() {
    assert!(Body::new(&[], IDENTITY).is_err());
    for shape in [
        ball([0.; 3], 0.),
        ball([0.; 3], f32::NAN),
        ball([f32::NAN, 0., 0.], 1.),
        polygon(vec![[0.; 3]], vec![[0, 1, 2]], false),
        polygon(vec![], vec![], false),
        polygon(
            vec![[0.; 3], [1., 0., 0.], [0., 1., 0.]],
            vec![[0, 1, 2]],
            true,
        ),
    ] {
        assert!(Body::new(&[shape], IDENTITY).is_err());
    }
    let shape = ball([0.; 3], 1.);
    for case in 0..4 {
        let mut m = IDENTITY;
        match case {
            0 => m[0] = -1.,
            1 => m[0] = 2.,
            2 => m[4] = 0.2,
            _ => m[12] = f32::NAN,
        };
        assert!(Body::new(&[shape.clone()], m).is_err());
    }
    let mut reversed_face = box_shape([0.; 3], [10.; 3], true);
    if let Surface::Mesh(m) = &mut reversed_face.surface {
        m.triangles[0].swap(0, 1);
    }
    assert!(Body::new(&[reversed_face], IDENTITY).is_err());
}

#[test]
fn closed_solid_winding_handles_small_facet_deviation_and_global_reversed_orientation() {
    let mut solid = box_shape([0.; 3], [10.; 3], true);
    if let Surface::Mesh(m) = &mut solid.surface {
        m.vertices[0][1] += 0.01;
    }
    let ball = body(&[ball([0.; 3], 1.)]);
    assert!(query(&body(&[solid.clone()]), &ball).intersects);
    if let Surface::Mesh(m) = &mut solid.surface {
        for t in &mut m.triangles {
            t.swap(0, 1);
        }
    }
    assert!(query(&body(&[solid]), &ball).intersects);
}
