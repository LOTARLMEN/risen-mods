//! World layers (`.lrent`): a GENOMFLE archive (body = u32 0xD0DEFADE + object) holding one `gCDynamicLayer` property object, whose class
//! data holds an `eCEntityDynamicContext` object with the entities. Field layout follows the Risen Editor
//! (RisenEditor-master/Code/Risen FileSystem/LRENT_Base.cs), version-dependent fields included.

use crate::archive::{Archive, StrRef};
use crate::bin_io::{ParseError, Reader, Result, Writer};
use crate::props::{ClassData, PropertyObject, format_guid};

/// `bCPropertyID`: GUID + 4 bytes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PropertyId {
    pub guid: [u8; 16],
    pub extra: u32,
}

impl PropertyId {
    pub(crate) fn read(r: &mut Reader) -> Result<Self> {
        Ok(PropertyId { guid: r.array()?, extra: r.u32()? })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.bytes(&self.guid);
        w.u32(self.extra);
    }

    pub fn text(&self) -> String {
        format_guid(&self.guid, true)
    }
}

/// `eCEntityProxy`: reference to another entity (template / creator).
#[derive(Debug, Clone)]
pub struct EntityProxy {
    pub version: u16,
    pub id: Option<PropertyId>,
}

impl EntityProxy {
    fn read(r: &mut Reader) -> Result<Self> {
        let version = r.u16()?;
        let id = if r.u8()? != 0 { Some(PropertyId::read(r)?) } else { None };
        Ok(EntityProxy { version, id })
    }

    fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        w.u8(self.id.is_some() as u8);
        if let Some(id) = &self.id {
            id.write(w);
        }
    }
}

/// Creator field of `eCDynamicEntity`: a proxy since version 213, a flag + id before.
#[derive(Debug, Clone)]
pub enum Creator {
    Proxy(EntityProxy),
    Old { flag: u8, id: Option<PropertyId> },
}

/// One `eCEntity` property set entry: u16, object, u32 (0xDEADBEEF in new files).
#[derive(Debug, Clone)]
pub struct EntityPropertySet {
    pub unknown: u16,
    pub object: PropertyObject,
    pub magic: u32,
}

#[derive(Debug, Clone)]
pub struct DynamicEntity {
    // eCDynamicEntity
    pub version: u16,
    pub version2: Option<u16>, // version <= 210
    pub id: PropertyId,
    pub name: StrRef,
    pub local_matrix: [f32; 16],
    pub world_box: [f32; 6],
    pub world_sphere: [f32; 4], // radius, center
    pub local_box: [f32; 6],
    pub creator: Creator,
    // eCGeometryEntity
    pub geo_version: u16,
    pub geo_unknown: Option<f32>, // geo_version <= 213
    pub world_matrix: [f32; 16],
    pub geo_box: [f32; 6],
    pub geo_sphere: [f32; 4],
    pub render_alpha_view_range: Option<[f32; 2]>, // geo_version >= 212 (Risen Editor: 213)
    pub geo_unknown16: Option<[u8; 16]>,           // geo_version < 212
    pub cache_in_range: Option<f32>,               // geo_version >= 213 (Risen Editor: 214, wrong)
    // eCEntity
    pub entity_version: u16,
    pub node_version: u16,
    pub enabled: u8,
    pub rendering_enabled: u8,
    pub unknown_b1: u8,
    pub flags_4000_8000_10000: Option<[u8; 3]>, // >= 211
    pub unknown_b2: Option<u8>,                 // <= 213
    pub unknown_b3: u8,
    pub picking_enabled: u8,
    pub collision_enabled: u8,
    pub insert_type: u16,
    pub locked: u8,
    pub unknown_b4: Option<u8>, // <= 212
    pub flag_200: u8,
    pub unknown_b5: Option<u8>, // <= 212
    pub data_changed: u64,      // FILETIME
    pub unknown_b67: Option<[u8; 2]>, // <= 212
    pub flag_40000: Option<u8>,       // >= 212
    pub save_game_relevant: Option<u8>, // >= 216
    pub property_sets: Vec<EntityPropertySet>,
}

impl DynamicEntity {
    fn read(r: &mut Reader, strings: &[String]) -> Result<Self> {
        let version = r.u16()?;
        let version2 = if version <= 210 { Some(r.u16()?) } else { None };
        let id = PropertyId::read(r)?;
        let name = StrRef::read(r)?;
        let local_matrix = r.f32s()?;
        let world_box = r.f32s()?;
        let world_sphere = r.f32s()?;
        let local_box = r.f32s()?;
        let creator = if version >= 213 {
            Creator::Proxy(EntityProxy::read(r)?)
        } else {
            let flag = r.u8()?;
            Creator::Old { flag, id: if flag != 0 { Some(PropertyId::read(r)?) } else { None } }
        };
        let geo_version = r.u16()?;
        let geo_unknown = if geo_version <= 213 { Some(r.f32()?) } else { None };
        let world_matrix = r.f32s()?;
        let geo_box = r.f32s()?;
        let geo_sphere = r.f32s()?;
        let (render_alpha_view_range, geo_unknown16) =
            if geo_version >= 212 { (Some(r.f32s()?), None) } else { (None, Some(r.array()?)) };
        let cache_in_range = if geo_version >= 213 { Some(r.f32()?) } else { None };
        let ev = r.u16()?;
        let node_version = r.u16()?;
        let enabled = r.u8()?;
        let rendering_enabled = r.u8()?;
        let unknown_b1 = r.u8()?;
        let flags_4000_8000_10000 = if ev >= 211 { Some(r.array()?) } else { None };
        let unknown_b2 = if ev <= 213 { Some(r.u8()?) } else { None };
        let unknown_b3 = r.u8()?;
        let picking_enabled = r.u8()?;
        let collision_enabled = r.u8()?;
        let insert_type = r.u16()?;
        let locked = r.u8()?;
        let unknown_b4 = if ev <= 212 { Some(r.u8()?) } else { None };
        let flag_200 = r.u8()?;
        let unknown_b5 = if ev <= 212 { Some(r.u8()?) } else { None };
        let data_changed = r.u64()?;
        let unknown_b67 = if ev <= 212 { Some(r.array()?) } else { None };
        let flag_40000 = if ev >= 212 { Some(r.u8()?) } else { None };
        let save_game_relevant = if ev >= 216 { Some(r.u8()?) } else { None };
        let count = r.u32()?;
        let mut property_sets = Vec::with_capacity(count.min(256) as usize);
        for i in 0..count {
            let start = r.pos();
            let unknown = r.u16()?;
            let object = PropertyObject::read(r, strings).map_err(|e| ParseError {
                offset: e.offset,
                msg: format!("v{version}/{geo_version}/{ev} node {node_version} property set {i}/{count} at 0x{start:X} (after {:?}): {}", property_sets.last().map(|p: &EntityPropertySet| p.object.class(strings)), e.msg),
            })?;
            property_sets.push(EntityPropertySet { unknown, object, magic: r.u32()? });
        }
        Ok(DynamicEntity {
            version,
            version2,
            id,
            name,
            local_matrix,
            world_box,
            world_sphere,
            local_box,
            creator,
            geo_version,
            geo_unknown,
            world_matrix,
            geo_box,
            geo_sphere,
            render_alpha_view_range,
            geo_unknown16,
            cache_in_range,
            entity_version: ev,
            node_version,
            enabled,
            rendering_enabled,
            unknown_b1,
            flags_4000_8000_10000,
            unknown_b2,
            unknown_b3,
            picking_enabled,
            collision_enabled,
            insert_type,
            locked,
            unknown_b4,
            flag_200,
            unknown_b5,
            data_changed,
            unknown_b67,
            flag_40000,
            save_game_relevant,
            property_sets,
        })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        if let Some(v) = self.version2 {
            w.u16(v);
        }
        self.id.write(w);
        self.name.write(w);
        w.f32s(&self.local_matrix);
        w.f32s(&self.world_box);
        w.f32s(&self.world_sphere);
        w.f32s(&self.local_box);
        match &self.creator {
            Creator::Proxy(p) => p.write(w),
            Creator::Old { flag, id } => {
                w.u8(*flag);
                if let Some(id) = id {
                    id.write(w);
                }
            }
        }
        w.u16(self.geo_version);
        if let Some(v) = self.geo_unknown {
            w.f32(v);
        }
        w.f32s(&self.world_matrix);
        w.f32s(&self.geo_box);
        w.f32s(&self.geo_sphere);
        if let Some(v) = &self.render_alpha_view_range {
            w.f32s(v);
        }
        if let Some(v) = &self.geo_unknown16 {
            w.bytes(v);
        }
        if let Some(v) = self.cache_in_range {
            w.f32(v);
        }
        w.u16(self.entity_version);
        w.u16(self.node_version);
        w.u8(self.enabled);
        w.u8(self.rendering_enabled);
        w.u8(self.unknown_b1);
        if let Some(v) = &self.flags_4000_8000_10000 {
            w.bytes(v);
        }
        if let Some(v) = self.unknown_b2 {
            w.u8(v);
        }
        w.u8(self.unknown_b3);
        w.u8(self.picking_enabled);
        w.u8(self.collision_enabled);
        w.u16(self.insert_type);
        w.u8(self.locked);
        if let Some(v) = self.unknown_b4 {
            w.u8(v);
        }
        w.u8(self.flag_200);
        if let Some(v) = self.unknown_b5 {
            w.u8(v);
        }
        w.u64(self.data_changed);
        if let Some(v) = &self.unknown_b67 {
            w.bytes(v);
        }
        if let Some(v) = self.flag_40000 {
            w.u8(v);
        }
        if let Some(v) = self.save_game_relevant {
            w.u8(v);
        }
        w.u32(self.property_sets.len() as u32);
        for ps in &self.property_sets {
            w.u16(ps.unknown);
            ps.object.write(w);
            w.u32(ps.magic);
        }
    }

    /// World position (translation row of the DirectX-style world matrix).
    pub fn position(&self) -> [f32; 3] {
        [self.world_matrix[12], self.world_matrix[13], self.world_matrix[14]]
    }

    /// Class names of the entity's property sets (gCNPC_PS, gCInventory_PS, ...).
    pub fn set_classes<'a>(&self, strings: &'a [String]) -> Vec<&'a str> {
        self.property_sets.iter().map(|ps| ps.object.class(strings)).collect()
    }

    pub fn property_set<'a>(&'a self, strings: &[String], class: &str) -> Option<&'a PropertyObject> {
        self.property_sets.iter().map(|ps| &ps.object).find(|o| o.class(strings) == class)
    }
}

#[derive(Debug, Clone)]
pub struct DynamicContext {
    pub version: u16,
    pub enabled: Option<u8>,           // >= 2
    pub unknown: Option<[f32; 2]>,     // 39..=211
    pub context_box: Option<[f32; 6]>, // >= 40
    pub entities: Vec<DynamicEntity>,
    /// (parent, child) index pairs, terminated by (-1, -1) which is kept in the list.
    pub hierarchy: Vec<(i32, i32)>,
}

impl DynamicContext {
    pub(crate) fn read(r: &mut Reader, strings: &[String]) -> Result<Self> {
        let version = r.u16()?;
        let enabled = if version >= 2 { Some(r.u8()?) } else { None };
        let unknown = if (39..=211).contains(&version) { Some(r.f32s()?) } else { None };
        let context_box = if version >= 40 { Some(r.f32s()?) } else { None };
        let count = r.u32()?;
        let mut entities = Vec::with_capacity(count.min(100_000) as usize);
        for i in 0..count {
            let start = r.pos();
            let e = DynamicEntity::read(r, strings).map_err(|e| ParseError {
                offset: e.offset,
                msg: format!("entity {i}/{count} at 0x{start:X}: {}", e.msg),
            })?;
            entities.push(e);
        }
        let hierarchy = read_hierarchy(r)?;
        Ok(DynamicContext { version, enabled, unknown, context_box, entities, hierarchy })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.u16(self.version);
        if let Some(v) = self.enabled {
            w.u8(v);
        }
        if let Some(v) = &self.unknown {
            w.f32s(v);
        }
        if let Some(v) = &self.context_box {
            w.f32s(v);
        }
        w.u32(self.entities.len() as u32);
        for e in &self.entities {
            e.write(w);
        }
        for &(p, c) in &self.hierarchy {
            w.i32(p);
            w.i32(c);
        }
    }
}

/// Class data of `gCDynamicLayer`: "GENOMEDL", u16 version, the entity context object.
#[derive(Debug, Clone)]
pub struct DynamicLayer {
    pub magic: u64,
    pub version: u16,
    pub context: PropertyObject,
}

impl DynamicLayer {
    pub(crate) fn read(r: &mut Reader, strings: &[String]) -> Result<Self> {
        Ok(DynamicLayer { magic: r.u64()?, version: r.u16()?, context: PropertyObject::read(r, strings)? })
    }

    pub(crate) fn write(&self, w: &mut Writer) {
        w.u64(self.magic);
        w.u16(self.version);
        self.context.write(w);
    }
}

/// A whole `.lrent` file.
#[derive(Debug, Clone)]
pub struct Lrent {
    pub archive: Archive,
    /// 0xD0DEFADE at the start of the body.
    pub body_magic: u32,
    pub root: PropertyObject,
}

impl Lrent {
    pub fn read(data: &[u8]) -> Result<Lrent> {
        let mut archive = Archive::read(data)?;
        let body = std::mem::take(&mut archive.body);
        let mut r = Reader::new(&body);
        let body_magic = r.u32()?;
        let root = PropertyObject::read(&mut r, &archive.strings)?;
        if r.remaining() != 0 {
            return r.err(format!("{} bytes after the layer object", r.remaining()));
        }
        Ok(Lrent { archive, body_magic, root })
    }

    pub fn write(&self) -> Vec<u8> {
        let mut w = Writer::default();
        w.u32(self.body_magic);
        self.root.write(&mut w);
        let archive = Archive { body: w.buf, ..self.archive.clone() };
        archive.write()
    }

    pub fn strings(&self) -> &[String] {
        &self.archive.strings
    }

    pub fn context(&self) -> Option<&DynamicContext> {
        let ClassData::DynamicLayer(layer) = &self.root.body.as_ref()?.data else { return None };
        match &layer.context.body.as_ref()?.data {
            ClassData::DynamicContext(c) => Some(c),
            _ => None,
        }
    }

    pub fn entities(&self) -> &[DynamicEntity] {
        self.context().map_or(&[], |c| &c.entities)
    }
}

/// Axis-aligned box as stored: min xyz, max xyz. Empty = min FLT_MAX, max -FLT_MAX.
pub fn box_union(a: [f32; 6], b: [f32; 6]) -> [f32; 6] {
    [a[0].min(b[0]), a[1].min(b[1]), a[2].min(b[2]), a[3].max(b[3]), a[4].max(b[4]), a[5].max(b[5])]
}

pub const EMPTY_BOX: [f32; 6] = [f32::MAX, f32::MAX, f32::MAX, -f32::MAX, -f32::MAX, -f32::MAX];

/// Bounding sphere (radius, center) around a box: center + half diagonal.
pub fn box_sphere(b: [f32; 6]) -> [f32; 4] {
    let h = [(b[3] - b[0]) / 2.0, (b[4] - b[1]) / 2.0, (b[5] - b[2]) / 2.0];
    [(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]).sqrt(), b[0] + h[0], b[1] + h[1], b[2] + h[2]]
}

impl DynamicContext {
    /// Root bounds recomputed from the entities: (root geo box, root geo sphere).
    pub fn computed_root_bounds(&self) -> ([f32; 6], [f32; 4]) {
        let b = self.entities.iter().skip(1).fold(EMPTY_BOX, |acc, e| box_union(acc, e.geo_box));
        (b, box_sphere(b))
    }
}

impl DynamicContext {
    /// Indices of the direct children of entity `i` (from the hierarchy pairs).
    pub fn children(&self, i: usize) -> Vec<usize> {
        self.hierarchy.iter().filter(|&&(p, c)| p == i as i32 && c >= 0).map(|&(_, c)| c as usize).collect()
    }

    pub fn parent(&self, i: usize) -> Option<usize> {
        self.hierarchy.iter().find(|&&(p, c)| c == i as i32 && p >= 0).map(|&(p, _)| p as usize)
    }

    /// Moves entity `i` (a direct child of the layer root) by `d` in world space, together with its
    /// descendants, then refreshes the root bounds. Rotation and local boxes stay unchanged.
    pub fn translate(&mut self, i: usize, d: [f32; 3]) -> std::result::Result<(), String> {
        if i == 0 || i >= self.entities.len() {
            return Err(format!("entity index {i} out of range 1..{}", self.entities.len()));
        }
        if self.parent(i) != Some(0) {
            return Err(format!("entity {i} is not a direct child of the layer root (not supported yet)"));
        }
        // Root matrix is identity in every layer, so local translation moves like world translation.
        let root = &self.entities[0];
        if root.world_matrix != IDENTITY {
            return Err("layer root matrix is not identity (not supported)".into());
        }
        for (m, dk) in self.entities[i].local_matrix[12..15].iter_mut().zip(d) {
            *m += dk;
        }
        let mut stack = vec![i];
        while let Some(j) = stack.pop() {
            let e = &mut self.entities[j];
            for (k, &dk) in d.iter().enumerate() {
                e.world_matrix[12 + k] += dk;
                e.world_box[k] += dk;
                e.world_box[3 + k] += dk;
                e.geo_box[k] += dk;
                e.geo_box[3 + k] += dk;
                e.world_sphere[1 + k] += dk;
                e.geo_sphere[1 + k] += dk;
            }
            stack.extend(self.children(j));
        }
        self.update_root_bounds();
        Ok(())
    }

    /// Root geo box = union of all entity geo boxes, sphere around it, context box = same box
    /// (true for all 410 shipped layers). Leaves everything untouched when the box didn't change,
    /// so the stored sphere (rounded differently by the original tools) survives.
    pub fn update_root_bounds(&mut self) {
        let (b, sphere) = self.computed_root_bounds();
        let root = &mut self.entities[0];
        if root.geo_box != b {
            root.geo_box = b;
            root.geo_sphere = sphere;
            if self.context_box.is_some() {
                self.context_box = Some(b);
            }
        }
    }
}

/// (parent, child) index pairs up to and including the (-1, -1) terminator.
pub(crate) fn read_hierarchy(r: &mut Reader) -> Result<Vec<(i32, i32)>> {
    let mut hierarchy = Vec::new();
    loop {
        let pair = (r.i32()?, r.i32()?);
        hierarchy.push(pair);
        if pair == (-1, -1) {
            return Ok(hierarchy);
        }
    }
}

pub const IDENTITY: [f32; 16] = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.];

impl Lrent {
    pub fn context_mut(&mut self) -> Option<&mut DynamicContext> {
        let ClassData::DynamicLayer(layer) = &mut self.root.body.as_mut()?.data else { return None };
        match &mut layer.context.body.as_mut()?.data {
            ClassData::DynamicContext(c) => Some(c),
            _ => None,
        }
    }

    /// Index of the entity with this GUID text ("{...}", braces optional, any case).
    pub fn find_guid(&self, guid: &str) -> Option<usize> {
        let g = guid.trim_matches(['{', '}']).to_uppercase();
        self.entities().iter().position(|e| e.id.text().trim_matches(['{', '}']) == g)
    }
}

/// Random version-4 GUID (std only: RandomState is seeded from OS randomness).
pub fn new_guid() -> [u8; 16] {
    use std::hash::{BuildHasher, Hasher};
    let half = || {
        let mut h = std::collections::hash_map::RandomState::new().build_hasher();
        h.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos()));
        h.finish().to_le_bytes()
    };
    let mut g = [0u8; 16];
    g[..8].copy_from_slice(&half());
    g[8..].copy_from_slice(&half());
    g[7] = (g[7] & 0x0F) | 0x40; // version 4 (Windows layout: Data3 is little endian)
    g[8] = (g[8] & 0x3F) | 0x80; // variant
    g
}

impl DynamicContext {
    /// Entity `i` and all its descendants, depth first, `i` first.
    pub fn subtree(&self, i: usize) -> Vec<usize> {
        let mut out = Vec::new();
        let mut stack = vec![i];
        while let Some(j) = stack.pop() {
            out.push(j);
            stack.extend(self.children(j).into_iter().rev());
        }
        out
    }

    /// Copies entity `i` (a direct child of the layer root) with its descendants to the end of the
    /// layer, with new GUIDs, at the same place. Returns the index of the copy. Property sets are
    /// copied byte for byte (same layer = same string table, so no string index remapping), except
    /// that GUIDs of the copied group inside them are replaced by the new ones.
    pub fn duplicate(&mut self, i: usize) -> std::result::Result<usize, String> {
        if i == 0 || i >= self.entities.len() {
            return Err(format!("entity index {i} out of range 1..{}", self.entities.len()));
        }
        if self.parent(i) != Some(0) {
            return Err(format!("entity {i} is not a direct child of the layer root (not supported yet)"));
        }
        let tree = self.subtree(i);
        let mut group: Vec<DynamicEntity> = tree.iter().map(|&j| self.entities[j].clone()).collect();
        renew_guids(&mut group);
        let pairs = group_pairs(self, &tree);
        Ok(self.append_group(group, &pairs))
    }

    /// Appends a copied group (subtree order, root first) as a new child of the layer root;
    /// `pairs` = the group's inner (parent, child) pairs as positions in the group. Returns the
    /// index of the group root.
    pub(crate) fn append_group(&mut self, group: Vec<DynamicEntity>, pairs: &[(usize, usize)]) -> usize {
        let base = self.entities.len();
        let mut new_pairs = vec![(0, base as i32)];
        new_pairs.extend(pairs.iter().map(|&(p, c)| ((base + p) as i32, (base + c) as i32)));
        self.entities.extend(group);
        let end = self.hierarchy.iter().position(|&p| p == (-1, -1)).unwrap_or(self.hierarchy.len());
        self.hierarchy.splice(end..end, new_pairs);
        base
    }
}

/// Inner (parent, child) pairs of the subtree `tree` of `c`, as positions in `tree`.
pub(crate) fn group_pairs(c: &DynamicContext, tree: &[usize]) -> Vec<(usize, usize)> {
    let pos = |x: i32| tree.iter().position(|&t| t as i32 == x);
    c.hierarchy.iter().filter_map(|&(p, ch)| Some((pos(p)?, pos(ch)?))).collect()
}

/// Gives every entity of a copied group a new GUID; references between members of the group
/// (inside their property sets) are pointed to the copies. Returns (old, new) GUIDs.
pub(crate) fn renew_guids(group: &mut [DynamicEntity]) -> Vec<([u8; 16], [u8; 16])> {
    let guids: Vec<([u8; 16], [u8; 16])> = group.iter().map(|e| (e.id.guid, new_guid())).collect();
    for (e, (_, new)) in group.iter_mut().zip(&guids) {
        e.id.guid = *new;
        for ps in &mut e.property_sets {
            for (old, new) in &guids {
                ps.object.replace_guid(old, new);
            }
        }
    }
    guids
}

impl Lrent {
    /// Moves entity `i` by `d` (see [`DynamicContext::translate`]) and keeps the pose copies in the
    /// property sets in step (see [`DynamicEntity::sync_pose`]).
    pub fn translate(&mut self, i: usize, d: [f32; 3]) -> std::result::Result<(), String> {
        let strings = self.archive.strings.clone();
        let c = self.context_mut().ok_or("layer has no entity context")?;
        c.translate(i, d)?;
        for j in c.subtree(i) {
            c.entities[j].sync_pose(&strings, None);
        }
        Ok(())
    }
}

impl DynamicEntity {
    /// Copies of the entity pose inside its property sets, set from the world matrix:
    /// gCNavigation_PS.LastUseableNavigationPosition (= entity position in every shipped layer;
    /// LastUseableNavigationZoneProxy is cleared, the zone belongs to the old place) and the two
    /// eCRigidBody_PS motions (= position + rotation quaternion in all 4915 shipped rigid bodies).
    /// The motion rotation is only set when `rotation` is given.
    pub fn sync_pose(&mut self, strings: &[String], rotation: Option<[f32; 4]>) {
        let pos = self.position();
        for ps in &mut self.property_sets {
            let class = ps.object.class(strings);
            let Some(body) = ps.object.body.as_mut() else { continue };
            match class {
                "gCNavigation_PS" => {
                    for p in &mut body.props.list {
                        match strings.get(p.name.0 as usize).map(String::as_str) {
                            Some("LastUseableNavigationPosition") if p.value.len() == 12 => {
                                p.value = pos.iter().flat_map(|v| v.to_le_bytes()).collect();
                            }
                            Some("LastUseableNavigationZoneProxy") if p.value.len() >= 3 => {
                                p.value = vec![p.value[0], p.value[1], 0];
                            }
                            _ => {}
                        }
                    }
                }
                "eCRigidBody_PS" => {
                    let ClassData::Raw(d) = &mut body.data else { continue };
                    let Some(at) = rigid_body_motions(d) else { continue };
                    for m in [at, at + 28] {
                        put_f32s(&mut d[m..m + 12], &pos);
                        if let Some(q) = rotation {
                            put_f32s(&mut d[m + 12..m + 28], &q);
                        }
                    }
                }
                _ => {}
            }
        }
    }
}

/// Offset of the two `bCMotion` (position xyz + quaternion xyzw, 28 bytes each) in eCRigidBody_PS
/// class data: u16 version, 2 bools (>= 40), bool (>= 41), 2 vectors (>= 51), motions (>= 55), bool (>= 65).
fn rigid_body_motions(d: &[u8]) -> Option<usize> {
    let v = u16::from_le_bytes([*d.first()?, *d.get(1)?]);
    let at = 2 + if v >= 40 { 2 } else { 0 } + if v >= 41 { 1 } else { 0 } + if v >= 51 { 24 } else { 0 };
    (v >= 55 && d.len() >= at + 56).then_some(at)
}

fn put_f32s(dst: &mut [u8], v: &[f32]) {
    for (k, x) in v.iter().enumerate() {
        dst[4 * k..4 * k + 4].copy_from_slice(&x.to_le_bytes());
    }
}

/// DirectX-style world matrix (row vectors, translation in m[12..15]) from a rotation quaternion
/// (x y z w, as in templates and rigid body motions) and a position.
pub fn quat_matrix(q: [f32; 4], pos: [f32; 3]) -> [f32; 16] {
    let [x, y, z, w] = q;
    [
        1. - 2. * (y * y + z * z), 2. * (x * y + z * w), 2. * (x * z - y * w), 0.,
        2. * (x * y - z * w), 1. - 2. * (x * x + z * z), 2. * (y * z + x * w), 0.,
        2. * (x * z + y * w), 2. * (y * z - x * w), 1. - 2. * (x * x + y * y), 0.,
        pos[0], pos[1], pos[2], 1.,
    ]
}

/// Axis-aligned box around a local box transformed by a world matrix (stored world boxes are
/// exactly this for all 49409 world entities with a creator).
pub fn transform_box(b: [f32; 6], m: &[f32; 16]) -> [f32; 6] {
    let mut out = EMPTY_BOX;
    for corner in 0..8 {
        let p = [b[if corner & 1 == 0 { 0 } else { 3 }], b[if corner & 2 == 0 { 1 } else { 4 }], b[if corner & 4 == 0 { 2 } else { 5 }]];
        for k in 0..3 {
            let v = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
            out[k] = out[k].min(v);
            out[3 + k] = out[3 + k].max(v);
        }
    }
    out
}

impl DynamicContext {
    /// Removes entity `i` (a direct child of the layer root) with its descendants, renumbers the
    /// hierarchy and refreshes the root bounds. Returns the removed entities in subtree order.
    pub fn remove(&mut self, i: usize) -> std::result::Result<Vec<DynamicEntity>, String> {
        if i == 0 || i >= self.entities.len() {
            return Err(format!("entity index {i} out of range 1..{}", self.entities.len()));
        }
        if self.parent(i) != Some(0) {
            return Err(format!("entity {i} is not a direct child of the layer root (not supported yet)"));
        }
        let tree = self.subtree(i);
        // New index of every kept entity (None = removed).
        let mut map = Vec::with_capacity(self.entities.len());
        let mut next = 0i32;
        for j in 0..self.entities.len() {
            if tree.contains(&j) {
                map.push(None);
            } else {
                map.push(Some(next));
                next += 1;
            }
        }
        self.hierarchy = self
            .hierarchy
            .iter()
            .filter_map(|&(p, c)| {
                if (p, c) == (-1, -1) {
                    return Some((p, c));
                }
                let m = |x: i32| if x < 0 { Some(x) } else { map[x as usize] };
                Some((m(p)?, m(c)?))
            })
            .collect();
        let mut removed = Vec::with_capacity(tree.len());
        let mut kept = Vec::with_capacity(self.entities.len() - tree.len());
        for (j, e) in std::mem::take(&mut self.entities).into_iter().enumerate() {
            if map[j].is_some() { kept.push(e) } else { removed.push((j, e)) }
        }
        self.entities = kept;
        removed.sort_by_key(|(j, _)| tree.iter().position(|t| t == j));
        self.update_root_bounds();
        Ok(removed.into_iter().map(|(_, e)| e).collect())
    }
}
