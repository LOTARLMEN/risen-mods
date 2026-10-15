//! Read-only Risen collision geometry, independent of the installed PhysX runtime.
//! Supported cooked streams: MESH 1 and CVXM 3 / CLHL 0 / CVHL 5 / VALE 2.
//! Source layout cross-checked with SDK 2.8.1 CookedMeshReader and native PhysX 2.8.4.
//! No axis swap: PhysX metres become Genome centimetres. Acceleration trees are skipped.
use crate::bin_io::{ParseError, Reader, Result};
use crate::geometry::Mesh;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Triangle,
    Convex,
}

#[derive(Debug, Clone)]
pub struct CookedMesh {
    pub kind: Kind,
    /// Index within the triangle OR convex array, as selected by eCCollisionShape.
    pub index: usize,
    pub stream_start: usize,
    pub stream_end: usize,
    pub mesh: Mesh,
    pub triangle_materials: Option<Vec<u16>>,
    /// Cooked local AABB, converted to centimetres.
    pub bounds: [f32; 6],
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Material {
    pub surface: u8,
    pub ignored_by_ray: bool,
    pub collision_disabled: bool,
    pub response_disabled: bool,
}

#[derive(Debug, Clone)]
pub struct CollisionMesh {
    pub meshes: Vec<CookedMesh>,
    /// Resource entries in triangle-array then convex-array order. Empty is permitted by the engine.
    pub materials: Vec<Material>,
    pub materials_offset: usize,
}

fn error(at: usize, msg: &str) -> ParseError {
    ParseError {
        offset: at,
        msg: msg.into(),
    }
}

/// An endian belongs to each nested PhysX header, not to the Genome wrapper.
#[derive(Clone, Copy)]
struct Endian(bool);
impl Endian {
    fn u16(self, r: &mut Reader) -> Result<u16> {
        let b = r.array()?;
        Ok(if self.0 {
            u16::from_le_bytes(b)
        } else {
            u16::from_be_bytes(b)
        })
    }
    fn u32(self, r: &mut Reader) -> Result<u32> {
        let b = r.array()?;
        Ok(if self.0 {
            u32::from_le_bytes(b)
        } else {
            u32::from_be_bytes(b)
        })
    }
    fn f32(self, r: &mut Reader) -> Result<f32> {
        Ok(f32::from_bits(self.u32(r)?))
    }
}

fn header(r: &mut Reader, magic: &[u8; 3], tag: &[u8; 4], version: u32) -> Result<Endian> {
    if r.bytes(3)? != magic {
        return r.err("invalid cooked stream signature");
    }
    let flags = r.u8()?;
    if flags > 1 {
        return r.err("unsupported cooked stream byte order flags");
    }
    let e = Endian(flags == 1);
    if r.bytes(4)? != tag || e.u32(r)? != version {
        return r.err("unsupported cooked chunk or version");
    }
    Ok(e)
}

fn skip(r: &mut Reader, count: usize, stride: usize) -> Result<()> {
    let size = count
        .checked_mul(stride)
        .ok_or_else(|| error(r.pos(), "cooked array size overflow"))?;
    r.bytes(size)?;
    Ok(())
}

fn width(maximum: u32) -> usize {
    if maximum <= 255 {
        1
    } else if maximum <= 65535 {
        2
    } else {
        4
    }
}
fn index(r: &mut Reader, e: Endian, stride: usize) -> Result<u32> {
    match stride {
        1 => Ok(r.u8()? as u32),
        2 => Ok(e.u16(r)? as u32),
        _ => e.u32(r),
    }
}
fn skip_indices(r: &mut Reader, e: Endian, count: usize, force16: bool) -> Result<()> {
    let maximum = e.u32(r)?;
    skip(
        r,
        count,
        if force16 {
            width(maximum).min(2)
        } else {
            width(maximum)
        },
    )
}

fn geometry(r: &mut Reader, e: Endian, nv: usize, nt: usize, stride: usize) -> Result<Mesh> {
    if nv == 0 || nt == 0 || nv > r.remaining() / 12 {
        return r.err("invalid cooked vertex/triangle count");
    }
    let mut mesh = Mesh::default();
    for _ in 0..nv {
        mesh.vertices
            .push([e.f32(r)? * 100., e.f32(r)? * 100., e.f32(r)? * 100.]);
    }
    read_triangles(r, e, &mut mesh, nt, stride)?;
    Ok(mesh)
}
fn read_triangles(
    r: &mut Reader,
    e: Endian,
    mesh: &mut Mesh,
    nt: usize,
    stride: usize,
) -> Result<()> {
    if nt == 0 || nt > r.remaining() / (3 * stride) {
        return r.err("invalid cooked triangle count");
    }
    for _ in 0..nt {
        mesh.triangles.push([
            index(r, e, stride)?,
            index(r, e, stride)?,
            index(r, e, stride)?,
        ]);
    }
    mesh.validate()
}

fn tree(r: &mut Reader, e: Endian) -> Result<()> {
    let size = e.u32(r)? as usize;
    let mut t = Reader::new(r.bytes(size)?);
    if t.bytes(3)? != b"OPC" {
        return t.err("missing OPC acceleration tree");
    }
    let flags = t.u8()?;
    if flags > 1 {
        return t.err("unsupported OPC endian flags");
    }
    let e = Endian(flags == 1);
    if e.u32(&mut t)? != 1 {
        return t.err("unsupported OPC version");
    }
    let code = e.u32(&mut t)?;
    if !matches!(code, 3 | 4) {
        return t.err("unsupported OPC model flags");
    }
    // Opaque tree data is length-delimited; do not interpret host pointers or rebuild physics.
    Ok(())
}

fn tail(r: &mut Reader, e: Endian) -> Result<[f32; 6]> {
    // Geometric epsilon, bounding sphere, AABB, mass, inertia tensor, centre of mass.
    skip(r, 5, 4)?;
    let mut bounds = [0.; 6];
    for v in &mut bounds {
        *v = e.f32(r)? * 100.;
    }
    skip(r, 13, 4)?;
    if !crate::bounds::valid_box(bounds) {
        return r.err("invalid cooked local bounds");
    }
    Ok(bounds)
}

fn triangle(r: &mut Reader) -> Result<(Mesh, Option<Vec<u16>>, [f32; 6])> {
    let e = header(r, b"NXS", b"MESH", 1)?;
    let flags = e.u32(r)?;
    if flags & !0x1f != 0 || flags & 4 != 0 || flags & 0x18 == 0x18 {
        return r.err("unsupported triangle serialization flags");
    }
    skip(r, 3, 4)?; // edge threshold, heightfield axis and extent
    let nv = e.u32(r)? as usize;
    let nt = e.u32(r)? as usize;
    let mesh = geometry(
        r,
        e,
        nv,
        nt,
        if flags & 8 != 0 {
            1
        } else if flags & 16 != 0 {
            2
        } else {
            4
        },
    )?;
    let materials = if flags & 1 != 0 {
        let mut out = Vec::with_capacity(nt);
        for _ in 0..nt {
            out.push(e.u16(r)?);
        }
        Some(out)
    } else {
        None
    };
    if flags & 2 != 0 {
        skip_indices(r, e, nt, false)?;
    }
    let convex = e.u32(r)?;
    let flat = e.u32(r)?;
    if convex != 0 {
        skip(r, nt, 2)?;
    }
    if flat != 0 {
        skip(r, nt, if flat < 256 { 1 } else { 2 })?;
    }
    tree(r, e)?;
    let bounds = tail(r, e)?;
    let extra = e.u32(r)? as usize;
    if extra != 0 && extra != nt {
        return r.err("invalid extra triangle data count");
    }
    r.bytes(extra)?;
    Ok((mesh, materials, bounds))
}

fn convex(r: &mut Reader, data: &[u8]) -> Result<(Mesh, Option<Vec<u16>>, [f32; 6])> {
    let outer = header(r, b"NXS", b"CVXM", 3)?;
    if outer.u32(r)? != 0 {
        return r.err("unsupported convex serialization flags");
    }
    header(r, b"ICE", b"CLHL", 0)?;
    let e = header(r, b"ICE", b"CVHL", 5)?;
    let nv = e.u32(r)? as usize;
    let nt = e.u32(r)? as usize;
    let edges = e.u32(r)? as usize;
    let polygons = e.u32(r)? as usize;
    let refs = e.u32(r)? as usize;
    let last = e.u32(r)? as usize;
    if nv == 0 || nv > 255 || nv > r.remaining() / 12 {
        return r.err("invalid convex vertex count");
    }
    let mut mesh = Mesh::default();
    for _ in 0..nv {
        mesh.vertices
            .push([e.f32(r)? * 100., e.f32(r)? * 100., e.f32(r)? * 100.]);
    }
    let maximum = e.u32(r)?;
    if maximum as usize >= nv {
        return r.err("invalid convex maximum vertex index");
    }
    read_triangles(r, e, &mut mesh, nt, width(maximum))?;
    let unquantized = e.u16(r)?;
    if unquantized > 1 {
        return r.err("invalid convex normals flag");
    }
    let normal_stride = if unquantized == 0 { 2 } else { 12 };
    skip(r, nv, normal_stride)?;
    skip(r, 1, 12)?; // hull centre
    // Original resources contain 32-bit disk pointers: always 36 bytes, even on a 64-bit host.
    skip(r, polygons, 36)?;
    skip(r, refs, 1)?;
    skip_indices(r, e, refs, true)?;
    skip(r, 2, 4)?;
    skip(r, edges, 2)?;
    skip(r, edges, normal_stride)?;
    skip_indices(r, e, edges, true)?;
    skip_indices(r, e, edges, true)?;
    skip_indices(r, e, edges, false)?;
    skip(r, last, 1)?;
    let v = header(r, b"ICE", b"VALE", 2)?;
    if v.u32(r)? as usize != nv {
        return r.err("convex valency vertex count differs");
    }
    let adjacent = v.u32(r)? as usize;
    skip_indices(r, v, nv, true)?;
    skip(r, adjacent, 1)?;
    tree(r, outer)?;
    let bounds = tail(r, outer)?;
    // Large hulls can carry a support map. It is tagged, not a guessed search for the next mesh.
    if data.get(r.pos()..r.pos().saturating_add(3)) == Some(b"ICE") {
        header(r, b"ICE", b"SUPM", 0)?;
        let g = header(r, b"ICE", b"GAUS", 0)?;
        let subdivision = g.u32(r)? as usize;
        let count = g.u32(r)? as usize;
        if subdivision == 0
            || subdivision
                .checked_mul(subdivision)
                .and_then(|n| n.checked_mul(6))
                != Some(count)
        {
            return r.err("invalid convex support-map size");
        }
        let size = count
            .checked_mul(2)
            .ok_or_else(|| error(r.pos(), "support map overflow"))?;
        if r.bytes(size)?.iter().any(|&i| i as usize >= nv) {
            return r.err("invalid support-map vertex index");
        }
    }
    Ok((mesh, None, bounds))
}

impl CollisionMesh {
    pub fn read(data: &[u8]) -> Result<Self> {
        if !data.starts_with(b"GR01CM00") {
            return Err(error(0, "not a collision mesh resource"));
        }
        let mut head = Reader::new(data);
        head.bytes(8)?;
        let object_at = head.u32()? as usize;
        let object_size = head.u32()? as usize;
        let payload_at = head.u32()? as usize;
        let payload_size = head.u32()? as usize;
        if object_at < 24
            || object_at.checked_add(object_size) != Some(payload_at)
            || payload_at.checked_add(payload_size) != Some(data.len())
        {
            return head.err("inconsistent collision resource offsets/sizes");
        }
        let (class, _, at) = crate::resource::properties(data)?;
        if class != "eCCollisionMeshResource2" {
            return Err(error(at, "unexpected collision mesh resource class"));
        }
        if at.checked_add(2) != Some(payload_at) {
            return Err(error(
                at,
                "collision payload offset differs from class data",
            ));
        }
        let mut r = Reader::new(data);
        r.bytes(at)?;
        if r.u16()? != 201 {
            return r.err("unsupported collision mesh class version");
        }
        let mut meshes = Vec::new();
        for kind in [Kind::Triangle, Kind::Convex] {
            let count = r.u32()? as usize;
            if count > r.remaining() / 12 {
                return r.err("invalid collision mesh array count");
            }
            for index in 0..count {
                let start = r.pos();
                let (mesh, triangle_materials, bounds) = match kind {
                    Kind::Triangle => triangle(&mut r)?,
                    Kind::Convex => convex(&mut r, data)?,
                };
                meshes.push(CookedMesh {
                    kind,
                    index,
                    stream_start: start,
                    stream_end: r.pos(),
                    mesh,
                    triangle_materials,
                    bounds,
                });
            }
        }
        let materials_offset = r.pos();
        let count = r.u32()? as usize;
        if count > r.remaining() / 4 {
            return r.err("invalid collision resource material count");
        }
        let mut materials = Vec::with_capacity(count);
        for _ in 0..count {
            let surface = r.u8()?;
            let boolean = |r: &mut Reader| -> Result<bool> {
                match r.u8()? {
                    0 => Ok(false),
                    1 => Ok(true),
                    _ => r.err("invalid collision resource boolean"),
                }
            };
            materials.push(Material {
                surface,
                ignored_by_ray: boolean(&mut r)?,
                collision_disabled: boolean(&mut r)?,
                response_disabled: boolean(&mut r)?,
            });
        }
        if r.remaining() != 0 {
            return r.err("bytes after collision resource");
        }
        Ok(Self {
            meshes,
            materials,
            materials_offset,
        })
    }

    pub fn get(&self, kind: Kind, index: usize) -> Option<&CookedMesh> {
        self.meshes
            .iter()
            .find(|m| m.kind == kind && m.index == index)
    }
}
