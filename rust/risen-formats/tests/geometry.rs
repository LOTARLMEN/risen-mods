use risen_formats::bin_io::Writer;
use risen_formats::geometry::{Mesh, actor_nodes, actor_meshes};
use risen_formats::lrent::{IDENTITY, quat_matrix};
use risen_formats::placement::{Side, snap, surface_offset};

fn text(w: &mut Writer, s: &str) { w.u16(s.len() as u16); w.bytes(s.as_bytes()); }
fn prop(w: &mut Writer, name: &str, ty: &str, value: &[u8]) {
    text(w, name); text(w, ty); w.u16(30); w.u32(value.len() as u32); w.bytes(value);
}
fn mesh_fixture() -> Vec<u8> {
    let mut part = Writer::default();
    part.u16(201); part.u32(1); prop(&mut part, "IndexCount", "long", &3u32.to_le_bytes());
    let mut sub = Writer::default();
    sub.u8(1); sub.u32(1); sub.u16(1); sub.u32(part.buf.len() as u32); sub.bytes(&part.buf);
    let mut w = Writer::default();
    w.bytes(b"GR01MS02"); w.u32(12);
    w.u16(1); w.u8(1); w.u16(1); w.u8(1); text(&mut w, "eCMeshResource2");
    w.bytes(&[1, 0, 0]); w.u16(200); w.u16(201); w.u32(0);
    w.u16(201); w.u32(1); prop(&mut w, "SubMeshes", "array", &sub.buf);
    w.u16(1);
    let mut decl = [0; 128];
    decl[..8].copy_from_slice(&[0, 0, 0, 0, 2, 0, 0, 0]); decl[12] = 17;
    w.bytes(&decl); w.u32(36); w.bytes(&[0; 12]); w.u32(12); w.bytes(&[0; 56]);
    for v in [[0., 2., 0.], [10., 2., 0.], [0., 12., 10.]] { w.f32s(&v); }
    w.u32(0); w.u32(1); w.u32(2);
    w.buf
}

#[test]
fn static_vertices_indices_and_bounds_are_native_coordinates() {
    let bytes = mesh_fixture();
    let m = Mesh::read_xmsh(&bytes).unwrap();
    assert_eq!(m.vertices, [[0., 2., 0.], [10., 2., 0.], [0., 12., 10.]]);
    assert_eq!(m.triangles, [[0, 1, 2]]);
    assert_eq!(m.bounds(), Some([0., 2., 0., 10., 12., 10.]));
    assert!((m.surface_y(2., 2., -100., 100.).unwrap() - 4.).abs() < 0.0001);
}

#[test]
fn truncated_and_bad_triangle_data_are_rejected() {
    let bytes = mesh_fixture();
    for n in 0..bytes.len() { assert!(Mesh::read_xmsh(&bytes[..n]).is_err(), "prefix {n}"); }
    let mut bad = bytes.clone(); let len = bad.len();
    bad[len - 4..].copy_from_slice(&99u32.to_le_bytes());
    assert!(Mesh::read_xmsh(&bad).is_err());
    let mut m = Mesh::read_xmsh(&bytes).unwrap(); m.vertices[0][1] = f32::NAN;
    assert!(m.validate().is_err());
}

fn actor_fixture(big: bool) -> Vec<u8> {
    let put = |w: &mut Writer, n: u32| w.bytes(&if big { n.to_be_bytes() } else { n.to_le_bytes() });
    let mut nodes = Writer::default(); put(&mut nodes, 2); put(&mut nodes, 0);
    for (i, name) in ["Root", "Bone_Hand"].into_iter().enumerate() {
        for v in [0f32, 0., 0., 1.] { put(&mut nodes, v.to_bits()); }
        nodes.bytes(&[0; 16]);
        for v in [i as f32, 2., 3.] { put(&mut nodes, v.to_bits()); }
        nodes.bytes(&[0; 32]); put(&mut nodes, if i == 0 { u32::MAX } else { 0 }); nodes.bytes(&[0; 76]);
        put(&mut nodes, name.len() as u32); nodes.bytes(name.as_bytes());
    }
    let mut xac = Writer::default(); xac.bytes(b"XAC"); xac.bytes(&[1, 0, 0, big as u8, 0]);
    put(&mut xac, 11); put(&mut xac, nodes.buf.len() as u32); put(&mut xac, 1); xac.bytes(&nodes.buf);
    let mut w = Writer::default(); w.bytes(b"GR01MA02"); w.bytes(&[0; 128]); w.u32(xac.buf.len() as u32); w.bytes(&xac.buf);
    w.buf
}

#[test]
fn actor_hierarchy_reads_both_byte_orders_and_refuses_truncation() {
    for big in [false, true] {
        let bytes = actor_fixture(big);
        let nodes = actor_nodes(&bytes).unwrap();
        assert_eq!(nodes.len(), 2); assert_eq!(nodes[0].parent, None); assert_eq!(nodes[1].parent, Some(0));
        assert_eq!(nodes[1].name, "Bone_Hand"); assert_eq!(nodes[1].position, [1., 2., 3.]);
        for n in 0..bytes.len() { assert!(actor_nodes(&bytes[..n]).is_err(), "prefix {n}"); }
    }
}

#[test]
fn actor_polygons_keep_split_vertices_and_node_binding_in_both_byte_orders() {
    for big in [false, true] {
        let mut bytes = actor_fixture(big);
        let put = |w: &mut Writer, n: u32| w.bytes(&if big { n.to_be_bytes() } else { n.to_le_bytes() });
        let mut mesh = Writer::default();
        for v in [1, 3, 3, 3, 1, 1, 0, 0, 12, 0] { put(&mut mesh, v); }
        for p in [[0f32, 0., 0.], [1., 0., 0.], [0., 0., 1.]] { for v in p { put(&mut mesh, v.to_bits()); } }
        for v in [3, 3, 0, 0, 0, 1, 2] { put(&mut mesh, v); }
        let mut chunk = Writer::default(); put(&mut chunk, 1); put(&mut chunk, mesh.buf.len() as u32); put(&mut chunk, 1); chunk.bytes(&mesh.buf);
        bytes.extend(chunk.buf);
        let len = bytes.len() - 140; bytes[136..140].copy_from_slice(&(len as u32).to_le_bytes());
        let meshes = actor_meshes(&bytes).unwrap();
        assert_eq!(meshes.len(), 1); assert_eq!(meshes[0].0, 1); assert_eq!(meshes[0].1.triangles, [[0, 1, 2]]);
        assert_eq!(meshes[0].1.vertices, [[0., 0., 0.], [1., 0., 0.], [0., 0., 1.]]);
        for n in 0..bytes.len() { assert!(actor_meshes(&bytes[..n]).is_err(), "prefix {n}"); }
    }
}

fn floor(height: f32) -> Mesh {
    Mesh { vertices: vec![[-100., height, -100.], [100., height, -100.], [100., height, 100.], [-100., height, 100.]],
        triangles: vec![[0, 1, 2], [0, 2, 3]] }
}

#[test]
fn surface_contact_handles_slope_edges_limits_and_both_face_windings() {
    let mut m = floor(4.);
    assert_eq!(m.surface_y(0., 0., 0., 10.), Some(4.));
    assert_eq!(m.surface_y(-100., 0., 0., 10.), Some(4.));
    assert_eq!(m.surface_y(101., 0., 0., 10.), None);
    assert_eq!(m.surface_y(0., 0., 5., 10.), None);
    for t in &mut m.triangles { t.swap(0, 2); }
    assert_eq!(m.surface_y(0., 0., 0., 10.), Some(4.));
    m.vertices[1][1] = 14.; m.vertices[2][1] = 14.;
    assert_eq!(m.surface_y(0., 0., 0., 20.), Some(9.));
    let b = [-10., 30., -10., 10., 40., 10.];
    assert!((surface_offset(b, &[m], 100., 0., 0.).unwrap() + 20.5).abs() < 0.0001);
}

#[test]
fn ground_moves_only_height_and_requires_support_at_all_samples() {
    let b = [-10., 20., -10., 10., 30., 10.];
    assert_eq!(surface_offset(b, &[floor(0.)], 100., 0., 0.).unwrap(), -20.);
    assert_eq!(surface_offset(b, &[floor(0.)], 100., 0., -2.).unwrap(), -22.);
    assert!(surface_offset(b, &[floor(0.)], 10., 0., 0.).is_err());
    assert!(surface_offset(b, &[floor(0.)], 100., 0., f32::NAN).is_err());
    let mut hole = floor(0.); hole.triangles.pop();
    assert!(surface_offset(b, &[hole], 100., 0., 0.).is_err());
}

#[test]
fn face_snap_supports_contact_clearance_overlap_and_centering() {
    let a = [0., 0., 0., 10., 20., 30.]; let b = [20., 40., 60., 30., 60., 90.];
    assert_eq!(snap(a, b, Side::Top, 0., false).unwrap(), [0., 60., 0.]);
    assert_eq!(snap(a, b, Side::Top, -5., true).unwrap(), [20., 55., 60.]);
    assert_eq!(snap(a, b, Side::Left, 2., false).unwrap(), [8., 0., 0.]);
    assert_eq!(snap(a, b, Side::Right, 2., false).unwrap(), [32., 0., 0.]);
    assert_eq!(snap(a, b, Side::Bottom, 2., false).unwrap(), [0., 18., 0.]);
    assert_eq!(snap(a, b, Side::Front, 2., false).unwrap(), [0., 0., 92.]);
    assert_eq!(snap(a, b, Side::Back, 2., false).unwrap(), [0., 0., 28.]);
    assert!(snap(a, b, Side::Top, f32::INFINITY, false).is_err());
}

#[test]
fn rotated_scaled_mesh_changes_world_contact_height() {
    let m = floor(0.);
    let mut matrix = quat_matrix([0., 0.5f32.sqrt(), 0., 0.5f32.sqrt()], [1000., 25., 2000.]);
    for k in [0, 2] { for j in 0..3 { matrix[k * 4 + j] *= 2.; } }
    let transformed = m.transformed(&matrix).unwrap();
    assert_eq!(transformed.surface_y(1000., 2000., 0., 100.), Some(25.));
    let mut bad = IDENTITY; bad[12] = f32::INFINITY;
    assert!(m.transformed(&bad).is_err());
}
