//! Independent serialized fixtures; no installed game required. In particular, disabled v200
//! sectors still contain layers, contrary to the old RisenEditor's parser.
use risen_formats::bin_io::Writer;
use risen_formats::props::ClassData;
use risen_formats::world::ProjectFile;

fn fixture(class: &str, data: &[u8], names: &[&str]) -> Vec<u8> {
    let mut body = Writer::default();
    body.u32(0xD0DE_FADE);
    body.u16(1); body.u8(1); // accessor version, valid
    body.u16(1); body.u8(1); body.u16(0); // unknowns, class index
    body.u16(1); body.u8(0); body.u16(200); body.u16(201);
    body.u32(17 + data.len() as u32);
    body.u16(201); body.u32(1); // property-list version, count
    body.u16(1); body.u16(2); body.u16(30); body.u32(1); body.u8(0x7F);
    body.bytes(data);
    let mut w = Writer::default();
    w.bytes(b"GENOMFLE"); w.u16(1); w.u32(14 + body.buf.len() as u32);
    w.bytes(&body.buf);
    w.u32(0xDEAD_BEEF); w.u8(1); w.u32(3 + names.len() as u32);
    for s in [class, "Spare", "bool"].into_iter().chain(names.iter().copied()) {
        w.u16(s.len() as u16); w.bytes(s.as_bytes());
    }
    w.buf
}

fn sector_data(version: u16, enabled: u8) -> Vec<u8> {
    let mut w = Writer::default();
    w.u16(version);
    if version >= 27 { w.u8(enabled); }
    if version >= 200 {
        w.u32(1); w.u32(0xAABB_CCDD); w.u16(3);
    }
    w.buf
}

#[test]
fn disabled_sector_keeps_its_layers_and_reserved_word() {
    for enabled in [0, 1, 7] {
        let bytes = fixture("gCSector", &sector_data(200, enabled), &["Story_Layer"]);
        let p = ProjectFile::read(&bytes).unwrap();
        let s = p.sector().unwrap();
        assert_eq!(s.enabled, Some(enabled));
        assert_eq!(s.reserved, Some(0xAABB_CCDD));
        assert_eq!(s.layers.len(), 1);
        assert_eq!(p.archive.str(s.layers[0]), "Story_Layer");
        assert!(p.world().is_none());
        assert_eq!(p.write(), bytes);
    }
}

#[test]
fn legacy_sector_versions_do_not_have_a_layer_list() {
    for version in [26, 27, 199] {
        let bytes = fixture("gCSector", &sector_data(version, 1), &[]);
        let p = ProjectFile::read(&bytes).unwrap();
        let s = p.sector().unwrap();
        assert_eq!(s.enabled, (version >= 27).then_some(1));
        assert_eq!(s.reserved, None);
        assert!(s.layers.is_empty());
        assert_eq!(p.write(), bytes);
    }
}

#[test]
fn world_data_reference_is_present_from_version_36() {
    for version in [35u16, 36, 200] {
        let mut data = version.to_le_bytes().to_vec();
        if version >= 36 { data.extend_from_slice(&3u16.to_le_bytes()); }
        let bytes = fixture("gCWorld", &data, &["#G3:/data/common/projects/World/World.wrldatasc"]);
        let p = ProjectFile::read(&bytes).unwrap();
        assert_eq!(p.world().unwrap().version, version);
        assert_eq!(p.world().unwrap().world_data_file.is_some(), version >= 36);
        assert!(p.sector().is_none());
        assert_eq!(p.write(), bytes);
    }
}

#[test]
fn typed_layer_list_can_be_edited_without_losing_properties() {
    let bytes = fixture("gCSector", &sector_data(200, 0), &["Old"]);
    let mut p = ProjectFile::read(&bytes).unwrap();
    let new = p.archive.intern("New");
    let b = p.root.body.as_mut().unwrap();
    let ClassData::Sector(s) = &mut b.data else { panic!("not a sector") };
    s.layers.push(new);
    let back = ProjectFile::read(&p.write()).unwrap();
    let names: Vec<_> = back.sector().unwrap().layers.iter().map(|&r| back.archive.str(r)).collect();
    assert_eq!(names, ["Old", "New"]);
    assert_eq!(back.sector().unwrap().enabled, Some(0));
    assert_eq!(back.root.body.as_ref().unwrap().props.list[0].value, [0x7F]);
}

#[test]
fn malformed_counts_references_and_trailers_are_rejected() {
    let mut data = sector_data(200, 0);
    data[3..7].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(ProjectFile::read(&fixture("gCSector", &data, &["Layer"])).is_err());
    let mut data = sector_data(200, 0);
    data[11..13].copy_from_slice(&99u16.to_le_bytes());
    assert!(ProjectFile::read(&fixture("gCSector", &data, &["Layer"])).is_err());
    let mut data = sector_data(200, 0);
    data.push(0);
    assert!(ProjectFile::read(&fixture("gCSector", &data, &["Layer"])).is_err());
    assert!(ProjectFile::read(&fixture("gCWorld", &[200, 0, 99, 0], &[])).is_err());
    assert!(ProjectFile::read(&fixture("UnknownClass", &[], &[])).is_err());
    let mut bytes = fixture("gCWorld", &[35, 0], &[]);
    bytes[14] ^= 1;
    assert!(ProjectFile::read(&bytes).is_err());
}

#[test]
fn every_truncated_prefix_is_rejected() {
    let bytes = fixture("gCSector", &sector_data(200, 0), &["Layer"]);
    for end in 0..bytes.len() {
        assert!(ProjectFile::read(&bytes[..end]).is_err(), "accepted prefix {end}");
    }
}

#[test]
fn string_remap_includes_sector_layers_and_world_data() {
    use risen_formats::strs::StrWalk;
    for (class, data) in [("gCSector", sector_data(200, 0)), ("gCWorld", vec![200, 0, 3, 0])] {
        let bytes = fixture(class, &data, &["ReferencedFile"]);
        let mut p = ProjectFile::read(&bytes).unwrap();
        let strings = p.archive.strings.clone();
        let last = strings.len() as u16 - 1;
        let mut flip = |r: &mut risen_formats::archive::StrRef| r.0 = last - r.0;
        let mut walk = StrWalk::new(&strings, &mut flip);
        walk.object(&mut p.root);
        assert!(walk.unknown.is_empty());
        p.archive.strings.reverse();
        let mut p = ProjectFile::read(&p.write()).unwrap();
        let r = p.sector().map(|s| s.layers[0]).or_else(|| p.world().unwrap().world_data_file).unwrap();
        assert_eq!(p.archive.str(r), "ReferencedFile");
        let strings = p.archive.strings.clone();
        let mut walk = StrWalk::new(&strings, &mut flip);
        walk.object(&mut p.root);
        assert!(walk.unknown.is_empty());
        p.archive.strings.reverse();
        assert_eq!(p.write(), bytes);
    }
}

#[test]
fn new_sector_drops_old_layer_names_and_enables_it() {
    let bytes = fixture("gCSector", &sector_data(200, 0), &["OldLayer"]);
    let donor = ProjectFile::read(&bytes).unwrap();
    let new = donor.sector_like(&["My_Items", "My_NPCs"]).unwrap();
    let new = ProjectFile::read(&new.write()).unwrap();
    let s = new.sector().unwrap();
    assert_eq!(s.enabled, Some(1));
    assert_eq!(s.reserved, Some(0));
    let names: Vec<_> = s.layers.iter().map(|&r| new.archive.str(r)).collect();
    assert_eq!(names, ["My_Items", "My_NPCs"]);
    assert!(!new.archive.strings.iter().any(|s| s == "OldLayer"));
    assert_eq!(donor.write(), bytes);
}
