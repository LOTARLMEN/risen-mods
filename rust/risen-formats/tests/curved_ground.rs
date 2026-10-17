use risen_formats::{collider::{self,Surface}, geometry::Mesh,
    lrent::{IDENTITY,quat_matrix}, placement::{ground_colliders_on,ground_plan_on}};

fn ball(center:[f32;3],radius:f32)->Surface { Surface::Capsule{a:center,b:center,radius} }
fn floor(a:f32,b:f32,center:[f32;3])->Surface {
    Surface::Mesh(Mesh{vertices:[[-200.,-200.],[200.,-200.],[200.,200.],[-200.,200.]].into_iter()
        .map(|[x,z]|[center[0]+x,center[1]+a*x+b*z,center[2]+z]).collect(),triangles:vec![[0,1,2],[0,2,3]]})
}
fn near(a:f32,b:f32) {assert!((a-b).abs()<0.02,"{a} != {b}");}

#[test]
fn analytic_lower_hits_clip_caps_and_cylinders_at_distant_coordinates() {
    for y in [100.,1_000_000.] {
        let sphere=ball([-37000.,y,-6000.],10.);
        assert_eq!(sphere.vertical_range(-37000.,-6000.,y-20.,y+20.),Some((y-10.,y+10.)));
        assert_eq!(sphere.vertical_range(-37000.,-6000.,y,y+20.),Some((y+10.,y+10.)));
        let tilted=Surface::Capsule{a:[-37010.,y-10.,-6000.],b:[-36990.,y+10.,-6000.],radius:5.};
        let (low,high)=tilted.vertical_range(-37000.,-6000.,y-20.,y+20.).unwrap();
        assert!((low-(y-5.*2f32.sqrt())).abs()<0.07);
        assert!((high-(y+5.*2f32.sqrt())).abs()<0.07);
    }
    assert!(ball([0.;3],1.).vertical_range(f32::NAN,0.,-10.,10.).is_none());
    assert!(ball([0.;3],1.).vertical_range(0.,0.,10.,-10.).is_none());
}

#[test]
fn offset_scaled_sphere_has_exact_horizontal_plane_contact_for_even_and_odd_grids() {
    for samples in [3,4,7,16,17] {
        let body=[ball([13.,7.,-9.],5.)];
        let mut world=quat_matrix([0.,0.5f32.sqrt(),0.,0.5f32.sqrt()],[-37000.,100.,-6000.]);
        for r in 0..3 {for k in 0..3 {world[4*r+k]*=2.;}}
        let plan=ground_colliders_on(&body,world,&[] as &[Surface],Some(20.),200.,100.,-3.,false,samples,45.).unwrap();
        near(plan.world_matrix[13],13.); // plane 20 + gap -3 - center 14 + radius 10
        assert_eq!(&plan.world_matrix[..12],&world[..12]);
        let b=body[0].transformed(&plan.world_matrix).unwrap().bounds().unwrap();
        near(b[1],17.);
        let again=ground_colliders_on(&body,plan.world_matrix,&[] as &[Surface],Some(20.),200.,100.,-3.,false,samples,45.).unwrap();
        assert_eq!(plan.world_matrix,again.world_matrix);
    }
}

#[test]
fn tilted_and_thin_capsules_include_exact_world_bottom_after_rotation() {
    for a in [0.,0.3,0.7853982,1.5707964] {
        let body=[Surface::Capsule{a:[0.,-80.,0.],b:[0.,80.,0.],radius:0.5}];
        let world=quat_matrix([0.,0.,(a/2f32).sin(),(a/2f32).cos()],[10.,200.,30.]);
        let plan=ground_colliders_on(&body,world,&[] as &[Surface],Some(10.),500.,100.,2.,false,4,45.).unwrap();
        let b=body[0].transformed(&plan.world_matrix).unwrap().bounds().unwrap();
        near(b[1],12.);
        assert_eq!(&plan.world_matrix[..12],&world[..12]);
    }
}

#[test]
fn compound_body_uses_lowest_enabled_shape_and_preserves_polygon_only_results() {
    let mesh=collider::box_mesh([0.;3],[10.,5.,10.],[1.,0.,0.,0.,1.,0.,0.,0.,1.]).unwrap();
    let body=[Surface::Mesh(mesh.clone()),ball([17.,-20.,0.],3.)];
    let mut world=IDENTITY;world[13]=100.;
    let surfaces=[floor(0.,0.,[0.;3])];
    let plan=ground_colliders_on(&body,world,&surfaces,None,200.,100.,0.,false,7,45.).unwrap();
    near(plan.world_matrix[13],23.);
    for align in [false,true] {
        let surfaces=[floor(0.1,-0.2,[0.;3])];
        let old=ground_plan_on(&mesh,world,&surfaces,None,200.,100.,-2.,align,7,45.).unwrap();
        let new=ground_colliders_on(&body[..1],world,&surfaces,None,200.,100.,-2.,align,7,45.).unwrap();
        assert_eq!(old.world_matrix,new.world_matrix);
        assert_eq!(old.samples,new.samples);
        assert_eq!(old.max_clearance,new.max_clearance);
    }
}

#[test]
fn curved_body_alignment_resamples_after_tilt_and_is_repeatable() {
    let body=[Surface::Capsule{a:[7.,-30.,-3.],b:[7.,30.,-3.],radius:5.}];
    let world=quat_matrix([0.,0.5f32.sqrt(),0.,0.5f32.sqrt()],[-37000.,150.,-6000.]);
    let surfaces=[floor(0.15,-0.2,[-37000.,20.,-6000.])];
    let p=ground_colliders_on(&body,world,&surfaces,None,400.,200.,-2.,true,9,45.).unwrap();
    near(p.normal[0],-0.15/(1.0625f32.sqrt()));
    near(p.normal[1],1./(1.0625f32.sqrt()));
    near(p.normal[2],0.2/(1.0625f32.sqrt()));
    // Independently compute continuous minimum clearance against this plane. A finite grid may
    // leave a small residual penetration, but rotated endpoint caps must not be ignored.
    let Surface::Capsule{a,b,radius}=body[0].transformed(&p.world_matrix).unwrap() else {panic!()};
    let clearance=|c:[f32;3]|c[1]-20.-0.15*(c[0]+37000.)+0.2*(c[2]+6000.);
    let minimum=clearance(a).min(clearance(b))-radius*(1.0625f32.sqrt());
    assert!((minimum+2.).abs()<0.15,"continuous plane clearance {minimum}");
    let again=ground_colliders_on(&body,p.world_matrix,&surfaces,None,400.,200.,-2.,true,9,45.).unwrap();
    for k in 0..16 {near(p.world_matrix[k],again.world_matrix[k]);}
    assert!(ground_colliders_on(&body,world,&surfaces,None,400.,200.,0.,true,9,5.).is_err());
}

#[test]
fn missing_support_invalid_shapes_poses_and_limits_refuse() {
    let valid=ball([0.;3],5.);
    for shape in [ball([0.;3],0.),ball([0.;3],f32::NAN),ball([f32::INFINITY,0.,0.],5.),Surface::Mesh(Mesh::default())] {
        assert!(ground_colliders_on(&[shape],IDENTITY,&[] as &[Surface],Some(0.),100.,100.,0.,false,7,45.).is_err());
    }
    for case in 0..5 {
        let mut m=IDENTITY;
        match case {0=>m[0]=2.,1=>m[0]=-1.,2=>m[4]=0.2,3=>m[12]=f32::NAN,_=>m[15]=0.}
        assert!(ground_colliders_on(&[valid.clone()],m,&[] as &[Surface],Some(0.),100.,100.,0.,false,7,45.).is_err());
    }
    for samples in [0,2,18,usize::MAX] {
        assert!(ground_colliders_on(&[valid.clone()],IDENTITY,&[] as &[Surface],Some(0.),100.,100.,0.,false,samples,45.).is_err());
    }
    assert!(ground_colliders_on(&[valid.clone()],IDENTITY,&[] as &[Surface],None,100.,100.,0.,false,7,45.).is_err());
    assert!(ground_colliders_on(&[valid],IDENTITY,&[] as &[Surface],Some(100.),1.,1.,0.,false,7,45.).is_err());
}
