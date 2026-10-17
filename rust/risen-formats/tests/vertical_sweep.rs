use risen_formats::{
    collider::{Shape, Surface, box_mesh},
    contact::{Body, query, query_horizontal_plane},
    geometry::Mesh,
    lrent::{IDENTITY, quat_matrix},
    sweep::{self, Target},
};
const O: [f32;9] = [1.,0.,0.,0.,1.,0.,0.,0.,1.];
fn body(surface:Surface,solid:bool)->Body {
    Body::new(&[Shape{surface,solid}],IDENTITY).unwrap()
}
fn ball(p:[f32;3],radius:f32)->Body {body(Surface::Capsule{a:p,b:p,radius},true)}
fn floor(y:f32)->Surface {
    Surface::Mesh(Mesh{vertices:vec![[-100.,y,-100.],[100.,y,-100.],[100.,y,100.],[-100.,y,100.]],
        triangles:vec![[0,1,2],[0,2,3]]})
}

#[test]
fn downward_sweep_corrects_the_bump_missed_by_sampled_ground() {
    let moving=body(Surface::Mesh(box_mesh([0.,1.,0.],[10.,1.,10.],O).unwrap()),true);
    let floor=body(floor(0.),false);
    let bump=body(Surface::Mesh(box_mesh([3.,0.5,3.],[0.1,0.5,0.1],O).unwrap()),true);
    let targets=[Target::Body(&floor),Target::Body(&bump)];
    let hit=sweep::downward(&moving,&targets,30.,-20.,0.001).unwrap();
    assert_eq!(hit.target,1);
    assert!((hit.offset_y_cm-1.).abs()<0.001);
    assert!(hit.offset_y_cm>1.);
    let placed=moving.translated([0.,hit.offset_y_cm,0.]).unwrap();
    let q=query(&placed,&bump);
    assert!(!q.intersects && q.distance_cm<=0.001);
    assert!(query(&placed,&floor).distance_cm>1.);
}

#[test]
fn first_contact_does_not_skip_thin_surfaces_or_choose_lower_disconnected_pockets() {
    let upper=body(floor(8.),false);
    let lower=body(floor(0.),false);
    let moving=ball([0.;3],0.01);
    let hit=sweep::downward(&moving,&[Target::Body(&lower),Target::Body(&upper)],20.,-20.,1e-5).unwrap();
    assert_eq!(hit.target,1);
    assert!((hit.offset_y_cm-8.01).abs()<1e-5);
    // Same geometry as one disconnected shell: no monotonic-overlap assumption or grid stepping.
    let Surface::Mesh(mut mesh)=floor(0.) else {unreachable!()};
    let Surface::Mesh(high)=floor(8.) else {unreachable!()};
    mesh.vertices.extend(high.vertices);
    mesh.triangles.extend(high.triangles.into_iter().map(|t|t.map(|i|i+4)));
    let combined=body(Surface::Mesh(mesh),false);
    let again=sweep::downward(&moving,&[Target::Body(&combined)],20.,-20.,1e-5).unwrap();
    assert!((again.offset_y_cm-hit.offset_y_cm).abs()<1e-8);
}

#[test]
fn plane_and_curved_compound_contacts_keep_known_clearance_and_witnesses() {
    for surface in [Surface::Capsule{a:[0.;3],b:[0.;3],radius:2.},
        Surface::Capsule{a:[-2.,-3.,0.],b:[2.,3.,0.],radius:2.}] {
        let moving=body(surface.clone(),true);
        let bottom=surface.bounds().unwrap()[1] as f64;
        let hit=sweep::downward(&moving,&[Target::HorizontalPlane(7.)],30.,-30.,0.001).unwrap();
        assert!(hit.offset_y_cm>=7.-bottom && hit.offset_y_cm<7.-bottom+0.001);
        let q=query_horizontal_plane(&moving.translated([0.,hit.offset_y_cm,0.]).unwrap(),7.).unwrap();
        assert!(!q.intersects && q.distance_cm<=0.001);
        assert!((q.point_b[1]-7.).abs()<1e-12);
    }
    let compound=Body::new(&[
        Shape{surface:Surface::Capsule{a:[0.;3],b:[0.;3],radius:2.},solid:true},
        Shape{surface:Surface::Mesh(box_mesh([10.,-5.,0.],[1.;3],O).unwrap()),solid:true},
    ],IDENTITY).unwrap();
    let hit=sweep::downward(&compound,&[Target::HorizontalPlane(0.)],20.,-20.,0.001).unwrap();
    assert!((hit.offset_y_cm-6.).abs()<0.001);assert_eq!(hit.contact.shape_a,1);
}

#[test]
fn slope_hit_uses_spatial_tolerance_without_claiming_vertical_error() {
    let Surface::Mesh(mut mesh)=floor(0.) else {unreachable!()};
    for p in &mut mesh.vertices {p[1]=p[0];}
    let support=body(Surface::Mesh(mesh),false);
    let moving=ball([0.;3],1.);
    let hit=sweep::downward(&moving,&[Target::Body(&support)],20.,-20.,0.001).unwrap();
    let expected=2f64.sqrt();
    assert!(hit.offset_y_cm>expected && hit.offset_y_cm<expected+0.001*2f64.sqrt());
    assert!(!hit.contact.intersects);
}

#[test]
fn starting_overlap_touch_side_surface_and_no_hit_refuse() {
    let moving=ball([0.;3],1.);
    for start in [0.,1.] {
        assert!(sweep::downward(&moving,&[Target::HorizontalPlane(0.)],start,-10.,0.01)
            .unwrap_err().contains("starts touching/overlapping"));
    }
    assert!(sweep::downward(&moving,&[Target::HorizontalPlane(0.)],10.,5.,0.01)
        .unwrap_err().contains("no downward contact"));
    // A parallel wall closer than tolerance must not masquerade as a ground support.
    let wall=body(Surface::Mesh(box_mesh([1.0015,0.,0.],[0.001,100.,100.],O).unwrap()),true);
    let targets=[Target::Body(&wall),Target::HorizontalPlane(-20.)];
    assert!(sweep::downward(&moving,&targets,10.,-30.,0.01).unwrap_err().contains("side/grazing"));
    assert!(sweep::downward(&moving,&targets,10.,-30.,1e-6).unwrap_err().contains("512 queries"));
}

#[test]
fn invalid_limits_translation_and_native_coordinate_overflow_refuse() {
    let moving=ball([0.;3],1.);
    let plane=[Target::HorizontalPlane(0.)];
    for (start,end,tolerance) in [(f64::NAN,-10.,0.01),(10.,f64::INFINITY,0.01),
        (0.,1.,0.01),(10.,-10.,0.),(10.,-10.,f64::NAN)] {
        assert!(sweep::downward(&moving,&plane,start,end,tolerance).is_err());
    }
    assert!(sweep::downward(&moving,&[],10.,-10.,0.01).is_err());
    assert!(moving.translated([f64::NAN,0.,0.]).is_err());
    assert!(moving.translated([f64::MAX,0.,0.]).is_err());
    assert!(sweep::downward(&moving,&[Target::HorizontalPlane(f64::NAN)],10.,-10.,0.01).is_err());
}

#[test]
fn translated_prepared_bvh_matches_fresh_native_pose_without_modifying_original() {
    let shapes=[Shape{surface:Surface::Mesh(box_mesh([2.,3.,4.],[2.,3.,4.],O).unwrap()),solid:true},
        Shape{surface:Surface::Capsule{a:[-10.,0.,0.],b:[-10.,20.,0.],radius:2.},solid:true}];
    let world=quat_matrix([0.,0.5f32.sqrt(),0.,0.5f32.sqrt()],[-37000.,1000.,-6000.]);
    let original=Body::new(&shapes,world).unwrap();
    let target=ball([-37000.,900.,-6000.],1.);
    let before=query(&original,&target);
    for y in [-20.,0.,50.] {
        let translated=original.translated([0.,y,0.]).unwrap();
        let mut pose=world;pose[13]+=y as f32;
        let fresh=Body::new(&shapes,pose).unwrap();
        let a=query(&translated,&target);let b=query(&fresh,&target);
        assert!((a.distance_cm-b.distance_cm).abs()<1e-9);assert_eq!(a.intersects,b.intersects);
        let after=query(&original,&target);
        assert_eq!(before.distance_cm,after.distance_cm);assert_eq!(before.point_a,after.point_a);
    }
}
