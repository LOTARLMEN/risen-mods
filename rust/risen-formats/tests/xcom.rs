use risen_formats::{
    bin_io::Writer,
    xcom::{CollisionMesh, Kind},
};

#[test]
fn original_collision_resource_extensions_resolve_to_compiled_xcom() {
    use risen_formats::resource::compiled_name;
    for source in [
        "Obj_Nat_DeadTree_Root_01_COL.xnvmsh",
        "folder/Obj_Nat_DeadTree_Root_01_COL.XNVMSH",
        "folder\\Obj_Nat_DeadTree_Root_01_COL.xcom",
        "Obj_Nat_DeadTree_Root_01_COL._xcom",
    ] {
        assert_eq!(
            compiled_name(source, "_xcom"),
            "Obj_Nat_DeadTree_Root_01_COL._xcom"
        );
    }
}

struct Cook {
    b: Vec<u8>,
    little: bool,
}
impl Cook {
    fn new(little: bool) -> Self {
        Self {
            b: Vec::new(),
            little,
        }
    }
    fn word(&mut self, n: u16) {
        self.b.extend(if self.little {
            n.to_le_bytes()
        } else {
            n.to_be_bytes()
        });
    }
    fn int(&mut self, n: u32) {
        self.b.extend(if self.little {
            n.to_le_bytes()
        } else {
            n.to_be_bytes()
        });
    }
    fn float(&mut self, v: f32) {
        self.int(v.to_bits());
    }
    fn zeros(&mut self, n: usize) {
        self.b.resize(self.b.len() + n, 0);
    }
    fn header(&mut self, magic: &[u8; 3], tag: &[u8; 4], version: u32) {
        self.b.extend(magic);
        self.b.push(self.little as u8);
        self.b.extend(tag);
        self.int(version);
    }
    fn index(&mut self, n: u32, width: usize) {
        match width {
            1 => self.b.push(n as u8),
            2 => self.word(n as u16),
            _ => self.int(n),
        }
    }
    fn tree(&mut self) {
        self.int(12);
        self.b.extend(b"OPC");
        self.b.push(self.little as u8);
        self.int(1);
        self.int(4);
    }
    fn tail(&mut self) {
        for v in [0.001, 0., 0., 0., 2., -1., -1., -1., 1., 1., 1.] {
            self.float(v);
        }
        self.zeros(13 * 4);
    }
}

fn triangle(little: bool, width: usize, materials: bool, parts: bool) -> Vec<u8> {
    let mut c = Cook::new(little);
    c.header(b"NXS", b"MESH", 1);
    c.int(
        2 | if materials { 1 } else { 0 }
            | match width {
                1 => 8,
                2 => 16,
                _ => 0,
            },
    );
    c.float(0.001);
    c.int(255);
    c.float(0.);
    c.int(3);
    c.int(1);
    for v in [0., 0., 0., 1., 0., 0., 0., 0., 1.] {
        c.float(v);
    }
    for i in [0, 1, 2] {
        c.index(i, width);
    }
    if materials {
        c.word(7);
    }
    c.int(0);
    c.b.push(0); // remap, one byte
    c.int(parts as u32);
    c.int(if parts { 256 } else { 0 });
    if parts {
        c.word(0);
        c.word(255);
    }
    c.tree();
    c.tail();
    c.int(1);
    c.b.push(0x3f);
    c.b
}

fn convex(little: bool, unquantized: bool, support: bool) -> Vec<u8> {
    let mut c = Cook::new(little);
    c.header(b"NXS", b"CVXM", 3);
    c.int(0);
    c.header(b"ICE", b"CLHL", 0);
    c.header(b"ICE", b"CVHL", 5);
    for n in [4, 4, 6, 4, 12, 12] {
        c.int(n);
    }
    for v in [0., 0., 0., 1., 0., 0., 0., 1., 0., 0., 0., 1.] {
        c.float(v);
    }
    c.int(3);
    c.b.extend([0, 1, 2, 0, 3, 1, 1, 3, 2, 0, 2, 3]);
    c.word(unquantized as u16);
    let stride = if unquantized { 12 } else { 2 };
    c.zeros(4 * stride + 12 + 4 * 36 + 12);
    c.int(0);
    c.zeros(12);
    c.zeros(8 + 6 * 2 + 6 * stride);
    for _ in 0..3 {
        c.int(0);
        c.zeros(6);
    }
    c.zeros(12);
    c.header(b"ICE", b"VALE", 2);
    c.int(4);
    c.int(12);
    c.int(3);
    c.b.extend([3; 4]);
    c.zeros(12);
    c.tree();
    c.tail();
    if support {
        c.header(b"ICE", b"SUPM", 0);
        c.header(b"ICE", b"GAUS", 0);
        c.int(1);
        c.int(6);
        c.zeros(12);
    }
    c.b
}

fn resource(triangles: &[Vec<u8>], convexes: &[Vec<u8>]) -> Vec<u8> {
    fn text(w: &mut Writer, s: &str) {
        w.u16(s.len() as u16);
        w.bytes(s.as_bytes());
    }
    let mut w = Writer::default();
    w.bytes(b"GR01CM00");
    w.bytes(&[0; 16]);
    let object_at = w.buf.len();
    w.u16(1);
    w.u8(1);
    w.u16(1);
    w.u8(1);
    text(&mut w, "eCCollisionMeshResource2");
    w.bytes(&[1, 0, 0]);
    w.u16(201);
    w.u16(201);
    w.u32(0);
    w.u16(201);
    w.u32(0);
    w.u16(201);
    let payload_at = w.buf.len();
    for array in [triangles, convexes] {
        w.u32(array.len() as u32);
        for chunk in array {
            w.bytes(chunk);
        }
    }
    w.u32(2);
    w.bytes(&[1, 0, 1, 0, 4, 1, 0, 1]);
    let sizes = [
        object_at,
        payload_at - object_at,
        payload_at,
        w.buf.len() - payload_at,
    ];
    for (i, n) in sizes.into_iter().enumerate() {
        w.buf[8 + i * 4..12 + i * 4].copy_from_slice(&(n as u32).to_le_bytes());
    }
    w.buf
}

#[test]
fn triangle_widths_endians_materials_and_multiple_array_indices() {
    for little in [true, false] {
        for width in [1, 2, 4] {
            for materials in [false, true] {
                let b = resource(
                    &[
                        triangle(little, width, materials, true),
                        triangle(!little, width, false, false),
                    ],
                    &[],
                );
                let c = CollisionMesh::read(&b).unwrap();
                assert_eq!(c.meshes.len(), 2);
                for i in 0..2 {
                    let m = c.get(Kind::Triangle, i).unwrap();
                    assert_eq!(
                        m.mesh.vertices,
                        [[0., 0., 0.], [100., 0., 0.], [0., 0., 100.]]
                    );
                    assert_eq!(m.mesh.triangles, [[0, 1, 2]]);
                    assert_eq!(m.bounds, [-100., -100., -100., 100., 100., 100.]);
                    assert_eq!(&b[m.stream_start..m.stream_start + 3], b"NXS");
                }
                assert_eq!(c.meshes[0].triangle_materials, materials.then_some(vec![7]));
                assert!(c.get(Kind::Convex, 0).is_none());
                assert!(c.get(Kind::Triangle, 2).is_none());
                assert_eq!(c.materials[0].surface, 1);
                assert!(c.materials[0].collision_disabled);
                assert!(c.materials[1].ignored_by_ray && c.materials[1].response_disabled);
            }
        }
    }
}

#[test]
fn convex_normals_support_map_and_separate_triangle_convex_indices() {
    for little in [true, false] {
        for normals in [false, true] {
            for support in [false, true] {
                let b = resource(
                    &[triangle(true, 1, false, false)],
                    &[convex(little, normals, support)],
                );
                let c = CollisionMesh::read(&b).unwrap();
                let m = c.get(Kind::Convex, 0).unwrap();
                assert_eq!(
                    m.mesh.vertices,
                    [[0., 0., 0.], [100., 0., 0.], [0., 100., 0.], [0., 0., 100.]]
                );
                assert_eq!(
                    m.mesh.triangles,
                    [[0, 1, 2], [0, 3, 1], [1, 3, 2], [0, 2, 3]]
                );
                assert_eq!(m.stream_end, c.materials_offset);
                assert!(m.triangle_materials.is_none());
                assert_eq!(c.get(Kind::Triangle, 0).unwrap().mesh.vertices.len(), 3);
            }
        }
    }
}

#[test]
fn every_truncated_prefix_is_refused_even_with_repaired_outer_lengths() {
    let b = resource(
        &[triangle(true, 2, true, true)],
        &[convex(false, true, true)],
    );
    let payload = u32::from_le_bytes(b[16..20].try_into().unwrap()) as usize;
    for n in 0..b.len() {
        assert!(CollisionMesh::read(&b[..n]).is_err(), "prefix {n}");
        if n >= payload {
            let mut cut = b[..n].to_vec();
            cut[20..24].copy_from_slice(&((n - payload) as u32).to_le_bytes());
            assert!(CollisionMesh::read(&cut).is_err(), "repaired prefix {n}");
        }
    }
}

#[test]
fn malformed_geometry_versions_sizes_flags_and_metadata_are_refused() {
    let base = resource(
        &[triangle(true, 1, true, false)],
        &[convex(true, true, true)],
    );
    let c = CollisionMesh::read(&base).unwrap();
    let t = c.meshes[0].stream_start;
    let hull = c.meshes[1].stream_start;
    for (at, bytes) in [
        (8, 0u32.to_le_bytes().to_vec()),
        (20, u32::MAX.to_le_bytes().to_vec()),
        (t + 8, 99u32.to_le_bytes().to_vec()),
        (t + 12, 0x1bu32.to_le_bytes().to_vec()), // conflicting index widths
        (t + 28, u32::MAX.to_le_bytes().to_vec()), // vertices
        (t + 32, u32::MAX.to_le_bytes().to_vec()), // triangles
        (t + 36, f32::NAN.to_le_bytes().to_vec()),
        (t + 36 + 36, vec![3]), // index outside the three vertices
        (hull + 36, 99u32.to_le_bytes().to_vec()), // hull version
        (hull + 40, 256u32.to_le_bytes().to_vec()), // convex count
        (c.materials_offset, u32::MAX.to_le_bytes().to_vec()),
        (c.materials_offset + 5, vec![2]),
    ] {
        let mut b = base.clone();
        b[at..at + bytes.len()].copy_from_slice(&bytes);
        assert!(CollisionMesh::read(&b).is_err(), "mutation {at:#x}");
    }
    let mut b = base.clone();
    let at = c.meshes[1].stream_end - 1;
    b[at] = 4;
    assert!(CollisionMesh::read(&b).is_err(), "bad support vertex");
    let mut b = base;
    b.push(0);
    let n = u32::from_le_bytes(b[20..24].try_into().unwrap()) + 1;
    b[20..24].copy_from_slice(&n.to_le_bytes());
    assert!(CollisionMesh::read(&b).is_err(), "trailing bytes");
}
