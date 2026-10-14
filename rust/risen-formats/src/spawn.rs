//! A new world entity built straight from a template (`.tple`), for templates with no world instance
//! to copy. Census of the 49409 world entities with a known creator template (TODO 5.21e):
//! * property sets = the template's sets, unchanged apart from instance state (NPC fight state,
//!   navigation, inventories filled by the level designers, lights) -> copied as they are; the class
//!   versions of templates and instances differ both ways (79 <-> 80), the engine reads both;
//! * name, render alpha / view range, insert type, date = the template's; creator = proxy v1 to the
//!   template; entity versions 214 / 214 / 217 / 210, cache-in-range 0 (all but 41 entities);
//! * world box = local box transformed by the world matrix, spheres = around the world box, local box
//!   = the mesh's `Boundary` (see [`crate::resource`]), passed in by the caller (it needs the paks);
//! * eCRigidBody_PS motions = world position + rotation (template: zero pose).

use crate::archive::StrRef;
use crate::lrent::{
    Creator, DynamicEntity, EntityPropertySet, EntityProxy, IDENTITY, Lrent, PropertyId, box_sphere, new_guid,
    quat_matrix, transform_box,
};
use crate::strs::{Remap, StrWalk};
use crate::tple::Tple;

/// Entities with one of these sets carry game state: SaveGameRelevant.
const SAVED: [&str; 6] = ["gCNPC_PS", "gCItem_PS", "gCInventory_PS", "gCLock_PS", "eCMover_PS", "gCStateGraph_PS"];

impl Lrent {
    /// Adds an entity of template `t` at `pos` (rotation = the template's) as a new child of the layer
    /// root; `local_box` = the mesh bounds. Returns its index. Nothing is changed on error
    /// (multi-template, a string index that can't be walked, ...).
    pub fn spawn_template(
        &mut self,
        t: &Tple,
        pos: [f32; 3],
        local_box: [f32; 6],
    ) -> std::result::Result<usize, String> {
        let te = t.template().ok_or("multi-template (a prefab of other templates): not supported")?;
        let c = self.context().ok_or("layer has no entity context")?;
        if c.entities.first().is_none_or(|root| root.world_matrix != IDENTITY) {
            return Err("layer root matrix is not identity (not supported)".into());
        }
        let ts = t.strings();
        let mut name = te.name;
        let mut sets: Vec<EntityPropertySet> = te
            .sets
            .iter()
            .map(|s| EntityPropertySet { unknown: s.id, object: s.object.clone(), magic: s.magic })
            .collect();
        let classes: Vec<String> = sets.iter().map(|s| s.object.class(ts).to_string()).collect();
        let has = |c: &str| classes.iter().any(|x| x == c);
        let mut rm = Remap::new(self.strings(), ts);
        let mut remap = |s: &mut StrRef| rm.remap(s);
        let mut w = StrWalk::new(ts, &mut remap);
        w.str(&mut name);
        for s in &mut sets {
            w.object(&mut s.object);
        }
        let mut unknown = w.unknown;
        if !unknown.is_empty() {
            unknown.sort();
            unknown.dedup();
            let list: Vec<String> = unknown.iter().map(|u| u.to_string()).collect();
            return Err(format!("template can't be put into a layer, unreadable: {}", list.join("; ")));
        }
        let strings = rm.finish()?;

        let flag = |k: usize| te.flags.get(k).copied().unwrap_or(0);
        let matrix = quat_matrix(te.rotation, pos);
        let world_box = transform_box(local_box, &matrix);
        let sphere = box_sphere(world_box);
        let mut e = DynamicEntity {
            version: 214,
            version2: None,
            id: PropertyId { guid: new_guid(), extra: 0 },
            name,
            local_matrix: matrix,
            world_box,
            world_sphere: sphere,
            local_box,
            creator: Creator::Proxy(EntityProxy { version: 1, id: Some(te.id) }),
            geo_version: 214,
            geo_unknown: None,
            world_matrix: matrix,
            geo_box: world_box,
            geo_sphere: sphere,
            render_alpha_view_range: Some([te.render_alpha, te.view_range]),
            geo_unknown16: None,
            cache_in_range: Some(0.0),
            entity_version: 217,
            node_version: 210,
            // Template flag bytes (v215+): enabled, rendering, ?, picking, collision, 0x200, ?, save game relevant.
            enabled: flag(0),
            rendering_enabled: flag(1),
            unknown_b1: 0,
            flags_4000_8000_10000: Some([1, 1, 1]),
            unknown_b2: None,
            // 1 for every entity with a mesh / animation / collision shape, 0 for lights, VFX, sounds.
            unknown_b3: ["eCMesh_PS", "eCAnimation_PS", "eCCollisionShape_PS", "eCSpeedTree_PS", "gCNPC_PS", "gCItem_PS"]
                .iter()
                .any(|c| has(c)) as u8,
            picking_enabled: flag(3),
            collision_enabled: flag(4),
            insert_type: te.insert_type,
            locked: 0,
            unknown_b4: None,
            flag_200: if te.version >= 215 { flag(5) } else { 0 },
            unknown_b5: None,
            data_changed: te.data_changed,
            unknown_b67: None,
            flag_40000: Some(0),
            // The template's flag or game state (every world item and NPC has it; NPC templates mostly don't).
            save_game_relevant: Some((te.version >= 215 && flag(7) != 0 || SAVED.iter().any(|c| has(c))) as u8),
            property_sets: sets,
        };
        e.sync_pose(&strings, Some(te.rotation));

        self.archive.strings = strings;
        let c = self.context_mut().ok_or("layer has no entity context")?;
        let i = c.append_group(vec![e], &[]);
        c.update_root_bounds();
        Ok(i)
    }
}
