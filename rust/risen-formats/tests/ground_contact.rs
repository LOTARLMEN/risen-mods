use risen_formats::{
    collider::{Shape, Surface, box_mesh},
    contact::{Body, query, query_horizontal_plane},
    geometry::Mesh,
    lrent::{IDENTITY, quat_matrix},
    placement::ground_colliders_on,
};

const O: [f32;9] = [1.,0.,0.,0.,1.,0.,0.,0.,1.];
fn shape(surface: Surface, solid: bool) -> Shape { Shape { surface, solid } }
fn body(shapes: &[Shape], world: [f32;16]) -> Body { Body::new(shapes,world).unwrap() }
fn near(a: f64, b: f64) { assert!((a-b).abs()<1e-6,"{a} != {b}"); }

#[test]
fn full_query_detects_thin_bump_missed_between_ground_samples() {
    let moving=box_mesh([0.,1.,0.],[10.,1.,10.],O).unwrap();
    let floor=Surface::Mesh(Mesh { vertices:vec![[-20.,0.,-20.],[20.,0.,-20.],
        [20.,0.,20.],[-20.,0.,20.]],triangles:vec![[0,1,2],[0,2,3]] });
    // Tiny support entirely between the 3x3 grid and model vertices; no underside sample hits it.
    let bump=Surface::Mesh(box_mesh([3.,0.5,3.],[0.1,0.5,0.1],O).unwrap());
    let surfaces=[floor.clone(),bump.clone()];
    let mut world=IDENTITY;world[13]=20.;
    let plan=ground_colliders_on(&[Surface::Mesh(moving.clone())],world,&surfaces,
        None,50.,10.,0.2,false,3,45.).unwrap();
    near(plan.world_matrix[13] as f64,0.2f32 as f64);
    let placed=body(&[shape(Surface::Mesh(moving),true)],plan.world_matrix);
    let baseline=query(&placed,&body(&[shape(floor,false)],IDENTITY));
    assert!(!baseline.intersects);near(baseline.distance_cm,0.2f32 as f64);
    let missed=query(&placed,&body(&[shape(bump,true)],IDENTITY));
    assert!(missed.intersects,"the full volume query must find the missed bump");
    // Diagnosis leaves the sampled pose unchanged; it does not silently correct placement.
    near(plan.world_matrix[13] as f64,0.2f32 as f64);
}

#[test]
fn plane_query_handles_curves_both_sides_touch_and_crossing() {
    for surface in [Surface::Capsule{a:[7.,10.,9.],b:[7.,10.,9.],radius:2.},
        Surface::Capsule{a:[7.,10.,9.],b:[12.,20.,15.],radius:2.}] {
        let b=body(&[shape(surface,true)],IDENTITY);
        for (height,distance) in [(0.,8.),(8.,0.),(10.,0.)] {
            let q=query_horizontal_plane(&b,height).unwrap();
            near(q.distance_cm,distance);assert_eq!(q.intersects,distance==0.);
            near(q.point_b[1],height);
            near((0..3).map(|k|(q.point_a[k]-q.point_b[k]).powi(2)).sum::<f64>().sqrt(),distance);
        }
        assert!(query_horizontal_plane(&b,100.).unwrap().distance_cm>0.);
    }
}

#[test]
fn plane_query_checks_each_triangle_in_disconnected_mesh_and_each_compound_shape() {
    // Whole-mesh Y bounds cross the plane, but neither disconnected triangle does.
    let shell=shape(Surface::Mesh(Mesh{vertices:vec![[0.,-2.,0.],[1.,-2.,0.],[0.,-2.,1.],
        [0.,3.,0.],[1.,3.,0.],[0.,3.,1.]],triangles:vec![[0,1,2],[3,4,5]]}),false);
    near(query_horizontal_plane(&body(&[shell.clone()],IDENTITY),0.).unwrap().distance_cm,2.);
    let sphere=shape(Surface::Capsule{a:[100.,0.,100.],b:[100.,0.,100.],radius:0.5},true);
    let q=query_horizontal_plane(&body(&[shell,sphere],IDENTITY),0.).unwrap();
    assert!(q.intersects);assert_eq!(q.shape_a,1);
    let b=body(&[shape(Surface::Capsule{a:[0.;3],b:[0.;3],radius:1.},true)],IDENTITY);
    for height in [f64::NAN,f64::INFINITY,f64::MAX] {
        assert!(query_horizontal_plane(&b,height).is_err());
    }
}

#[test]
fn plane_query_preserves_rotated_scaled_distant_pose_and_solid_boundary() {
    let surface=Surface::Mesh(box_mesh([0.;3],[1.,2.,3.],O).unwrap());
    let mut world=quat_matrix([0.,0.,0.5f32.sqrt(),0.5f32.sqrt()],[-37000.,1000000.,-6000.]);
    for r in 0..3 { for k in 0..3 {world[4*r+k]*=2.;} }
    let b=body(&[shape(surface,true)],world);
    let q=query_horizontal_plane(&b,999990.).unwrap();
    near(q.distance_cm,8.); // allow the original f32 quaternion's tiny rotation rounding
    assert!(query_horizontal_plane(&b,1000000.).unwrap().intersects);
    assert!(query_horizontal_plane(&b,1000010.).unwrap().distance_cm>7.99);
}
