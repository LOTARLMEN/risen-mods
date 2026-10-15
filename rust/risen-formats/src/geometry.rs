//! Read-only geometry in native Risen coordinates (centimetres, Y up).
//! Static render meshes expose triangles; collision meshes remain distinct cooked PhysX resources.
use crate::bin_io::{Reader, Result};
use crate::lrent::{EMPTY_BOX, box_union};
use crate::resource::{inline_properties, properties};

#[derive(Debug, Clone, Default)]
pub struct Mesh {
    pub vertices: Vec<[f32; 3]>,
    pub triangles: Vec<[u32; 3]>,
}

impl Mesh {
    pub fn transformed(&self, matrix: &[f32; 16]) -> Result<Self> {
        let vertices = self.vertices.iter().map(|p| {
            let p = crate::lrent::transform_vector(*p, matrix);
            [p[0] + matrix[12], p[1] + matrix[13], p[2] + matrix[14]]
        }).collect();
        let out = Mesh { vertices, triangles: self.triangles.clone() };
        out.validate()?;
        Ok(out)
    }

    pub fn read_xmsh(data: &[u8]) -> Result<Self> {
        let (class, props, at) = properties(data)?;
        let mut r = Reader::new(&data[at..]);
        if class != "eCMeshResource2" { return r.err("not a static mesh"); }
        let mut indices = 0usize;
        if let Some(p) = props.iter().find(|p| p.name == "SubMeshes") {
            let mut s = Reader::new(&p.value);
            s.u8()?; // array version; the enclosing property size was already consumed
            let count = s.u32()? as usize;
            if count > s.remaining() / 12 { return s.err("invalid submesh count"); }
            for _ in 0..count {
                s.u16()?;
                let size = s.u32()? as usize;
                let mut part = Reader::new(s.bytes(size)?);
                for p in inline_properties(&mut part)? {
                    if p.name == "IndexCount" {
                        let mut v = Reader::new(&p.value);
                        indices = indices.checked_add(v.u32()? as usize).ok_or_else(|| crate::bin_io::ParseError { offset: at, msg: "index count overflow".into() })?;
                    }
                }
            }
        }
        if !indices.is_multiple_of(3) { return r.err("triangle index count is not divisible by three"); }
        r.u16()?;
        let declarations = r.bytes(128)?;
        let mut position = None;
        for d in declarations.chunks_exact(8) {
            if d[4] == 17 { break; }
            if d[..2] == [0, 0] && d[4..] == [2, 0, 0, 0] {
                position = Some(u16::from_le_bytes([d[2], d[3]]) as usize);
            }
        }
        let position = position.ok_or_else(|| crate::bin_io::ParseError { offset: at + r.pos(), msg: "no float3 position stream".into() })?;
        let bytes = r.u32()? as usize;
        r.bytes(12)?;
        let stride = r.u32()? as usize;
        r.bytes(56)?;
        if stride == 0 || !bytes.is_multiple_of(stride) || position + 12 > stride { return r.err("invalid vertex layout"); }
        let stream = r.bytes(bytes)?;
        let mut mesh = Mesh::default();
        mesh.vertices.reserve(bytes / stride);
        for vertex in stream.chunks_exact(stride) {
            mesh.vertices.push(Reader::new(&vertex[position..position + 12]).f32s()?);
        }
        if indices > r.remaining() / 4 { return r.err("truncated triangle indices"); }
        for _ in 0..indices / 3 { mesh.triangles.push([r.u32()?, r.u32()?, r.u32()?]); }
        mesh.validate()?;
        Ok(mesh)
    }

    pub fn validate(&self) -> Result<()> {
        let r = Reader::new(&[]);
        if self.vertices.iter().flatten().any(|v| !v.is_finite()) { return r.err("nonfinite mesh vertex"); }
        if self.triangles.iter().flatten().any(|&i| i as usize >= self.vertices.len()) { return r.err("triangle index outside vertices"); }
        Ok(())
    }

    pub fn bounds(&self) -> Option<[f32; 6]> {
        (!self.vertices.is_empty()).then(|| self.vertices.iter().fold(EMPTY_BOX,
            |b, p| box_union(b, [p[0], p[1], p[2], p[0], p[1], p[2]])))
    }

    /// Highest two-sided triangle intersection on a vertical segment. f64 barycentrics avoid
    /// cancellation at world coordinates far from the origin; vertical/degenerate faces are skipped.
    pub fn surface_y(&self, x: f32, z: f32, bottom: f32, top: f32) -> Option<f32> {
        self.vertical_range(x, z, bottom, top).map(|(_, highest)| highest)
    }

    /// Lowest and highest vertical triangle hits, including either face winding.
    pub fn vertical_range(&self, x: f32, z: f32, bottom: f32, top: f32) -> Option<(f32, f32)> {
        let mut lowest: Option<f32> = None;
        let mut highest: Option<f32> = None;
        for t in &self.triangles {
            let [a, b, c] = t.map(|i| self.vertices[i as usize].map(f64::from));
            let den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
            if den.abs() < 1e-12 { continue; }
            let u = ((b[2] - c[2]) * (x as f64 - c[0]) + (c[0] - b[0]) * (z as f64 - c[2])) / den;
            let v = ((c[2] - a[2]) * (x as f64 - c[0]) + (a[0] - c[0]) * (z as f64 - c[2])) / den;
            if u < -1e-7 || v < -1e-7 || u + v > 1. + 1e-7 { continue; }
            let y = (u * a[1] + v * b[1] + (1. - u - v) * c[1]) as f32;
            if y >= bottom && y <= top {
                if lowest.is_none_or(|old| y < old) { lowest = Some(y); }
                if highest.is_none_or(|old| y > old) { highest = Some(y); }
            }
        }
        lowest.zip(highest)
    }
}

#[derive(Debug, Clone)]
pub struct ActorNode {
    pub name: String,
    pub parent: Option<usize>,
    pub rotation: [f32; 4],
    pub position: [f32; 3],
}

/// XAC node hierarchy embedded in ._xmac. Bind-pose values, not live animation state.
/// Mesh/skin/animation chunks are left separate; a node is not necessarily a deforming bone.
pub fn actor_nodes(data: &[u8]) -> Result<Vec<ActorNode>> {
    let mut head = Reader::new(data);
    if head.bytes(8)? != b"GR01MA02" { return head.err("not an actor resource"); }
    head.bytes(128)?;
    let len = head.u32()? as usize;
    let xac = head.bytes(len)?;
    let mut r = Reader::new(xac);
    if r.bytes(3)? != b"XAC" { return r.err("missing XAC stream"); }
    r.bytes(3)?;
    let big = r.u8()? != 0;
    r.u8()?;
    let mut nodes = Vec::new();
    while r.remaining() != 0 {
        let read_u32 = |r: &mut Reader| -> Result<u32> { let b = r.array()?; Ok(if big { u32::from_be_bytes(b) } else { u32::from_le_bytes(b) }) };
        let id = read_u32(&mut r)?;
        let size = read_u32(&mut r)? as usize;
        let version = read_u32(&mut r)?;
        let mut chunk = Reader::new(r.bytes(size)?);
        if id != 11 { continue; }
        if version != 1 { return chunk.err(format!("unsupported XAC node version {version}")); }
        let n = read_u32(&mut chunk)? as usize;
        read_u32(&mut chunk)?;
        if n > chunk.remaining() / 156 { return chunk.err("invalid XAC node count"); }
        for i in 0..n {
            let mut rotation = [0.; 4];
            for v in &mut rotation { *v = f32::from_bits(read_u32(&mut chunk)?); }
            chunk.bytes(16)?;
            let mut position = [0.; 3];
            for v in &mut position { *v = f32::from_bits(read_u32(&mut chunk)?); }
            chunk.bytes(32)?;
            let parent = read_u32(&mut chunk)? as usize;
            chunk.bytes(76)?;
            let length = read_u32(&mut chunk)? as usize;
            let name = String::from_utf8_lossy(chunk.bytes(length)?).into_owned();
            if parent < n && parent >= i { return chunk.err("XAC parent must precede child"); }
            if position.iter().chain(&rotation).any(|v| !v.is_finite()) { return chunk.err("nonfinite XAC node pose"); }
            nodes.push(ActorNode { name, parent: (parent < n).then_some(parent), rotation, position });
        }
        if chunk.remaining() != 0 { return chunk.err("bytes after XAC nodes"); }
        // Subsequent XAC material chunks have vendor-specific size conventions. They are not
        // needed for the node hierarchy; do not mistake their payload for another chunk header.
        return Ok(nodes);
    }
    Ok(nodes)
}

/// Raw bind-pose render meshes in an XAC resource, with their owning node indices.
/// Skin weights and live bone deformation are not applied. Duplicate split vertices are retained.
pub fn actor_meshes(data: &[u8]) -> Result<Vec<(usize, Mesh)>> {
    let nodes = actor_nodes(data)?;
    let mut h = Reader::new(data);
    h.bytes(136)?; let len = h.u32()? as usize;
    let xac = h.bytes(len)?;
    let big = xac[6] != 0;
    let u32_value = |r: &mut Reader| -> Result<u32> { let b = r.array()?; Ok(if big { u32::from_be_bytes(b) } else { u32::from_le_bytes(b) }) };
    let mut at = 8;
    let mut meshes = Vec::new();
    while at < xac.len() {
        let mut r = Reader::new(&xac[at..]);
        let id = u32_value(&mut r)?;
        let size = u32_value(&mut r)? as usize;
        u32_value(&mut r)?;
        let next = at.checked_add(12).and_then(|v| v.checked_add(size))
            .filter(|&v| v <= xac.len()).ok_or_else(|| crate::bin_io::ParseError { offset: at, msg: "XAC chunk outside stream".into() })?;
        if id == 13 {
            // Material chunk lengths in the shipped EMotionFX exporter can understate their payload.
            // Walk the material names and maps to find the next real section header.
            let n = u32_value(&mut r)? as usize; r.bytes(8)?;
            if n > r.remaining() / 100 { return r.err("invalid XAC material count"); }
            for _ in 0..n {
                r.bytes(95)?; let maps = r.u8()?;
                let length = u32_value(&mut r)? as usize; r.bytes(length)?;
                for _ in 0..maps {
                    r.bytes(28)?;
                    let length = u32_value(&mut r)? as usize; r.bytes(length)?;
                }
            }
            at += r.pos();
            continue;
        }
        if id == 1 {
            let mut r = Reader::new(&xac[at + 12..next]);
            let node = u32_value(&mut r)? as usize;
            if node >= nodes.len() { return r.err("mesh node outside XAC hierarchy"); }
            u32_value(&mut r)?; // welded/base vertex count
            let n = u32_value(&mut r)? as usize;
            let indices = u32_value(&mut r)? as usize;
            u32_value(&mut r)?; // submesh count
            let layers = u32_value(&mut r)? as usize; r.bytes(4)?;
            if !indices.is_multiple_of(3) || layers > r.remaining() / 12 { return r.err("invalid XAC mesh counts"); }
            let mut vertices = None;
            for _ in 0..layers {
                let kind = u32_value(&mut r)?; let stride = u32_value(&mut r)? as usize; r.bytes(4)?;
                let size = stride.checked_mul(n).ok_or_else(|| crate::bin_io::ParseError { offset: r.pos(), msg: "XAC vertex layer overflows".into() })?;
                let mut layer = Reader::new(r.bytes(size)?);
                if kind == 0 {
                    if stride != 12 { return layer.err("unsupported XAC position layout"); }
                    let mut v = Vec::with_capacity(n);
                    for _ in 0..n {
                        v.push([f32::from_bits(u32_value(&mut layer)?), f32::from_bits(u32_value(&mut layer)?), f32::from_bits(u32_value(&mut layer)?)]);
                    }
                    vertices = Some(v);
                }
            }
            let vertices = vertices.ok_or_else(|| crate::bin_io::ParseError { offset: r.pos(), msg: "XAC mesh has no positions".into() })?;
            let mut mesh = Mesh { vertices, triangles: Vec::new() };
            let mut passed = 0u32;
            while mesh.triangles.len() < indices / 3 {
                let part = u32_value(&mut r)? as usize;
                let count = u32_value(&mut r)?;
                u32_value(&mut r)?; // material
                let bones = u32_value(&mut r)? as usize;
                if part == 0 || !part.is_multiple_of(3) || part / 3 > indices / 3 - mesh.triangles.len() || part > r.remaining() / 4 {
                    return r.err("invalid XAC submesh indices");
                }
                for _ in 0..part / 3 {
                    let mut t = [0; 3];
                    for v in &mut t {
                        *v = u32_value(&mut r)?.checked_add(passed).ok_or_else(|| crate::bin_io::ParseError { offset: r.pos(), msg: "XAC triangle index overflows".into() })?;
                    }
                    mesh.triangles.push(t);
                }
                if bones > r.remaining() / 4 { return r.err("invalid XAC bone palette"); }
                r.bytes(bones * 4)?;
                passed = passed.checked_add(count).ok_or_else(|| crate::bin_io::ParseError { offset: r.pos(), msg: "XAC vertex count overflows".into() })?;
            }
            mesh.validate()?;
            meshes.push((node, mesh));
        }
        at = next;
    }
    Ok(meshes)
}
