//! Read-only transform census/check against projects.pak.
use risen_formats::{lrent::{Lrent, matrix_product}, pak::Pak};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(std::env::args().nth(1).ok_or("projects.pak path")?)?;
    let mut nested = 0;
    let mut checked = 0;
    let mut yaw_ok = 0;
    let mut refused = std::collections::BTreeMap::<String, usize>::new();
    let mut max_error = 0.0f32;
    let mut max_geo_error = 0.0f32;
    let mut max_parent_geo_error = 0.0f32;
    let census_only = std::env::args().any(|a| a == "--census-only");
    for entry in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
        let bytes = pak.read(entry)?;
        let l = Lrent::read(&bytes)?;
        let c = l.context().ok_or("no context")?;
        let first_rigid = c.entities.iter().enumerate().skip(1)
            .find(|(_, e)| e.set_classes(l.strings()).contains(&"eCRigidBody_PS")).map(|(i, _)| i);
        for i in 1..c.entities.len() {
            let aggregate = c.children(i).iter().fold(c.entities[i].world_box,
                |b, &j| risen_formats::lrent::box_union(b, c.entities[j].geo_box));
            let error = aggregate.iter().zip(c.entities[i].geo_box).map(|(a, b)| (a - b).abs()).fold(0., f32::max);
            max_geo_error = max_geo_error.max(error);
            if error > max_parent_geo_error && !c.children(i).is_empty() {
                max_parent_geo_error = error;
                if census_only { println!("parent geo mismatch {} #{i}, error {error}, own {:?}, geo {:?}, children {:?}", entry.path,
                    c.entities[i].world_box, c.entities[i].geo_box, c.children(i)); }
            }
            if let Some(p) = c.parent(i).filter(|&p| p != 0) {
                nested += 1;
                let world = matrix_product(&c.entities[i].local_matrix, &c.entities[p].world_matrix);
                for (a, b) in world.iter().zip(c.entities[i].world_matrix) {
                    max_error = max_error.max((a - b).abs());
                }
            }
            if census_only { continue; }
            if i != 1 && first_rigid != Some(i) && c.parent(i) == Some(0) && c.children(i).is_empty() { continue; }
            let tree = c.subtree(i);
            let mut moved = l.clone();
            moved.translate(i, [64., -32., 16.]).map_err(|err| format!("{} #{i}: {err}", entry.path))?;
            for &j in &tree {
                let a = &moved.entities()[j]; let b = &l.entities()[j];
                for k in 0..3 { assert!((a.position()[k] - b.position()[k] - [64., -32., 16.][k]).abs() < 0.02); }
                if j != i { assert_eq!(a.local_matrix, b.local_matrix); }
                let p = c.parent(j).unwrap();
                let world = matrix_product(&a.local_matrix, &moved.entities()[p].world_matrix);
                let old = matrix_product(&b.local_matrix, &l.entities()[p].world_matrix);
                for k in 0..16 {
                    assert!((world[k] - old[k] - (a.world_matrix[k] - b.world_matrix[k])).abs() < 0.04,
                        "{} #{j} matrix[{k}] changed inconsistently", entry.path);
                }
            }
            let mut ancestors = Vec::new();
            let mut p = c.parent(i).unwrap();
            while p != 0 { ancestors.push(p); p = c.parent(p).unwrap(); }
            let mut untouched = moved.clone();
            let back = untouched.context_mut().unwrap();
            for &j in tree.iter().chain(&ancestors).chain(std::iter::once(&0)) { back.entities[j] = c.entities[j].clone(); }
            back.context_box = c.context_box;
            assert_eq!(untouched.write(), bytes, "changed unrelated data: {} #{i}", entry.path);
            let out = moved.write(); assert_eq!(Lrent::read(&out)?.write(), out);
            checked += 1;
            let pivot = moved.entities()[i].position();
            let before_yaw = moved.write();
            match moved.set_yaw(i, 90.) {
                Ok(()) => {
                    assert_eq!(moved.entities()[i].position(), pivot);
                    let m = &moved.entities()[i].world_matrix;
                    if m[8].hypot(m[10]) > 1e-6 { assert!(m[10].abs() < 0.002 && m[8] > 0.); }
                    for &j in &tree {
                        let e = &moved.entities()[j];
                        let world = matrix_product(&e.local_matrix, &moved.entities()[c.parent(j).unwrap()].world_matrix);
                        for k in 0..16 { assert!((world[k] - e.world_matrix[k]).abs() < 0.04, "yaw matrix mismatch {} #{j}", entry.path); }
                    }
                    let out = moved.write(); assert_eq!(Lrent::read(&out)?.write(), out);
                    yaw_ok += 1;
                }
                Err(err) => { assert_eq!(moved.write(), before_yaw); *refused.entry(err).or_default() += 1; }
            }
        }
        assert_eq!(l.write(), bytes);
    }
    println!("{nested} nested entities, max original local*parent/world difference {max_error}, max node/children geo-box difference {max_geo_error} (parents {max_parent_geo_error}); {checked} subtree moves and {yaw_ok} yaw edits/readbacks; refusals: {refused:?}; source layers unchanged");
    Ok(())
}
