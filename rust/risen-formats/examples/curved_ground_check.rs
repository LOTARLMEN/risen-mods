//! Read-only check of authored curved template colliders against a horizontal plane.
use risen_formats::{collider::{self,Surface}, collision::shapes, lrent::quat_matrix,
    placement::ground_colliders_on, tple::Tple, vfs::GameVfs};

fn main()->Result<(),Box<dyn std::error::Error>> {
    let vfs=GameVfs::open(std::env::args().nth(1).ok_or("game directory required")?)?;
    let mut curved=0;
    let mut supported=0;
    let mut checks=0;
    let mut spheres=0;
    let mut capsules=0;
    let mut refused=std::collections::BTreeMap::<String,usize>::new();
    for entry in vfs.files("data/common/templates",".tple") {
        let bytes=vfs.read(entry)?;
        let template=Tple::read(&bytes)?;
        let Some(entity)=template.template() else {continue};
        let objects:Vec<_>=entity.sets.iter().map(|s|&s.object).collect();
        let curved_shape=objects.iter().filter(|o|o.class(template.strings())=="eCCollisionShape_PS")
            .map(|o|shapes(o,template.strings())).collect::<Result<Vec<_>,_>>()?
            .iter().flatten().any(|s|matches!(s.kind,4|5));
        if !curved_shape {continue;}
        curved+=1;
        let body=match collider::surfaces(&objects,template.strings(),&risen_formats::lrent::IDENTITY,false,None,
            |_|Err("resource collider present; primitive-only corpus check".into())) {
            Ok(body) if body.iter().any(|s|matches!(s,Surface::Capsule{..}))=>body,
            Ok(_)=>{*refused.entry("curved shapes disabled/filtered".into()).or_default()+=1;continue;},
            Err(e)=>{*refused.entry(e).or_default()+=1;continue;},
        };
        supported+=1;
        if supported<=10 {println!("candidate {}",entry.path);}
        if body.iter().any(|s|matches!(s,Surface::Capsule{a,b,..} if a==b)) {
            spheres+=1;
            if spheres==1 {println!("sphere candidate {}",entry.path);}
        }
        if body.iter().any(|s|matches!(s,Surface::Capsule{a,b,..} if a!=b)) {capsules+=1;}
        for angle in [0.,0.4,1.5707964] {for samples in [4,9] {
            let mut world=quat_matrix([0.,0.,(angle/2f32).sin(),(angle/2f32).cos()],[-37000.,500.,-6000.]);
            for r in 0..3 {for k in 0..3 {world[4*r+k]*=1.5;}}
            let plan=ground_colliders_on(&body,world,&[] as &[Surface],Some(20.),5000.,5000.,-2.,false,samples,45.)
                .map_err(|e|format!("{}: {e}",entry.path))?;
            let transformed=body.iter().map(|s|s.transformed(&plan.world_matrix)).collect::<Result<Vec<_>,_>>()?;
            let bounds=collider::bounds(&transformed).ok_or("empty ground body")?;
            assert!((bounds[1]-18.).abs()<0.05,"{} bottom {}",entry.path,bounds[1]);
            assert_eq!(&plan.world_matrix[..12],&world[..12]);
            let again=ground_colliders_on(&body,plan.world_matrix,&[] as &[Surface],Some(20.),5000.,5000.,-2.,false,samples,45.)?;
            assert!((again.world_matrix[13]-plan.world_matrix[13]).abs()<0.01,"{} repeated ground",entry.path);
            checks+=1;
        }}
        assert_eq!(template.write(),bytes,"template data changed");
    }
    assert!(supported>0,"no supported original curved templates checked");
    println!("curved templates {curved}; supported primitive bodies {supported} ({spheres} with spheres, {capsules} with capsules); placements {checks}; skipped {refused:?}");
    Ok(())
}
