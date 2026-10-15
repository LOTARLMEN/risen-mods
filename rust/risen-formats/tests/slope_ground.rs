use risen_formats::geometry::Mesh;
use risen_formats::lrent::{IDENTITY, quat_matrix, transform_vector};
use risen_formats::placement::{ground_plan, surface_offset};

fn board() -> Mesh {
    Mesh { vertices: vec![[-20.,0.,-20.],[20.,0.,-20.],[20.,0.,20.],[-20.,0.,20.],
        [-20.,2.,-20.],[20.,2.,-20.],[20.,2.,20.],[-20.,2.,20.]],
        triangles: vec![[0,1,2],[0,2,3],[4,6,5],[4,7,6],
            [0,4,5],[0,5,1],[1,5,6],[1,6,2],[2,6,7],[2,7,3],[3,7,4],[3,4,0]] }
}
fn floor(a: f32, b: f32, center: [f32;3]) -> Mesh {
    let vertices = [[-200.,-200.],[200.,-200.],[200.,200.],[-200.,200.]].into_iter()
        .map(|[x,z]|[center[0]+x, center[1]+a*x+b*z, center[2]+z]).collect();
    Mesh {vertices,triangles:vec![[0,1,2],[0,2,3]]}
}
fn unit(v:[f32;3])->[f32;3] { let n=v.iter().map(|v|v*v).sum::<f32>().sqrt(); v.map(|v|v/n) }

#[test]
fn alignment_fits_plane_at_large_coordinates_preserving_heading_scale_and_gap() {
    for yaw in [0.,45.,90.,230.] {
        let angle: f32=yaw*std::f32::consts::PI/360.;
        let mut world=quat_matrix([0.,angle.sin(),0.,angle.cos()],[-37000.,50.,-6000.]);
        for r in 0..3 { for k in 0..3 { world[4*r+k]*=[2.,3.,4.][r]; } }
        let surface=floor(0.1,-0.2,[-37000.,10.,-6000.]);
        let plan=ground_plan(&board(),world,&[surface.clone()],None,200.,100.,-2.,true,7,45.).unwrap();
        let normal=unit([-0.1,1.,0.2]);
        for k in 0..3 { assert!((plan.normal[k]-normal[k]).abs()<0.0001); }
        assert!((plan.world_matrix[8].atan2(plan.world_matrix[10])-world[8].atan2(world[10])).abs()<0.0001);
        for r in 0..3 {
            let scale=(0..3).map(|k|plan.world_matrix[4*r+k].powi(2)).sum::<f32>().sqrt();
            assert!((scale-[2.,3.,4.][r]).abs()<0.0001);
        }
        for p in &board().vertices[..4] {
            let v=transform_vector(*p,&plan.world_matrix);
            let p: [f32;3]=std::array::from_fn(|k|v[k]+plan.world_matrix[12+k]);
            let ground=surface.surface_y(p[0],p[2],-100.,100.).unwrap();
            assert!((p[1]-ground+2.).abs()<0.02);
        }
        assert!(plan.max_clearance<0.03);
        let twice=ground_plan(&board(),plan.world_matrix,&[surface],None,200.,100.,-2.,true,7,45.).unwrap();
        for k in 0..16 { assert!((twice.world_matrix[k]-plan.world_matrix[k]).abs()<0.02); }
    }
}

#[test]
fn dense_sampling_catches_interior_bump_missed_by_five_points_without_changing_orientation() {
    let mut world=IDENTITY;world[13]=20.;
    let base=floor(0.,0.,[0.;3]);
    let bump=Mesh{vertices:vec![[4.,10.,4.],[6.,10.,4.],[6.,10.,6.],[4.,10.,6.]],triangles:vec![[0,1,2],[0,2,3]]};
    let surfaces=[base,bump];
    assert_eq!(surface_offset([-20.,20.,-20.,20.,22.,20.],&surfaces,100.,100.,0.).unwrap(),-20.);
    let dense=ground_plan(&board(),world,&surfaces,None,100.,100.,0.,false,9,45.).unwrap();
    assert!((dense.world_matrix[13]-10.).abs()<0.001);
    assert_eq!(&dense.world_matrix[..12],&world[..12]);
    assert!((dense.max_clearance-10.).abs()<0.001);
    assert!(dense.samples>9*9);
}

#[test]
fn explicit_plane_levels_a_tilted_board_and_keeps_negative_gap() {
    let world=quat_matrix([0.2f32.sin(),0.,0.,0.2f32.cos()],[0.,30.,0.]);
    let plan=ground_plan(&board(),world,&[],Some(10.),100.,100.,-3.,true,7,45.).unwrap();
    assert_eq!(plan.normal,[0.,1.,0.]);
    assert!((plan.world_matrix[13]-7.).abs()<0.001);
    for k in [1,2,4,6,8,9] { assert!(plan.world_matrix[k].abs()<0.0001); }
}

#[test]
fn missing_support_steep_shear_reflection_and_invalid_settings_are_refused() {
    let flat=floor(0.,0.,[0.;3]);
    let mut missing=flat.clone();missing.triangles.pop();
    assert!(ground_plan(&board(),IDENTITY,&[missing],None,100.,100.,0.,true,7,45.).is_err());
    assert!(ground_plan(&board(),IDENTITY,&[floor(1.,0.,[0.;3])],None,100.,100.,0.,true,7,20.).is_err());
    for case in 0..9 {
        let mut world=IDENTITY;
        let mut samples=7;
        let mut gap=0.;
        let mut limit=45.;
        match case {
            0=>world[4]=0.2,
            1=>world[0]=-1.,
            2=>world[0]=0.,
            3=>world[12]=f32::NAN,
            4=>samples=2,
            5=>samples=18,
            6=>gap=f32::INFINITY,
            7=>limit=90.,
            _=>world[3]=1.,
        }
        assert!(ground_plan(&board(),world,&[flat.clone()],None,100.,100.,gap,true,samples,limit).is_err(),"case {case}");
    }
}
