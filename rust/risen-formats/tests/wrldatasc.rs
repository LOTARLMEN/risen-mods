use risen_formats::wrldatasc::WorldData;

#[test]
fn round_trip_preserves_bom_comments_unknown_sections_and_single_byte_text() {
    let data = b"\xEF\xBB\xBF[Common]\r\n;\xFF comment\r\nOther=keep\n [Sector.List] \r\n Story = TRUE ; note\r\nDisabled=0\n[Future]\nSomething=else";
    let w = WorldData::read(data).unwrap();
    assert_eq!(w.write(), data);
    assert_eq!(w.sectors().len(), 2);
    assert_eq!(w.sectors()[0].name, "Story");
    assert!(w.sectors()[0].enabled);
    assert!(!w.sectors()[1].enabled);
}

#[test]
fn toggle_changes_only_the_value_and_is_idempotent() {
    let data = b"[Sector.List]\r\n Story = FALSE ; keep this\r\nOther=1\r\n";
    let mut w = WorldData::read(data).unwrap();
    w.set_sector("story", true).unwrap();
    assert_eq!(w.write(), b"[Sector.List]\r\n Story = true ; keep this\r\nOther=1\r\n");
    let before = w.write();
    w.set_sector("STORY", true).unwrap();
    w.set_sector("Other", true).unwrap();
    assert_eq!(w.write(), before);
}

#[test]
fn insert_stays_in_sector_section_and_preserves_existing_bytes() {
    let mut w = WorldData::read(b"[Sector.List]\r\nOld=true\r\n[Future]\r\nKeep=1").unwrap();
    w.set_sector("New_Layer", true).unwrap();
    assert_eq!(w.write(), b"[Sector.List]\r\nOld=true\r\nNew_Layer=true\r\n[Future]\r\nKeep=1");
    assert_eq!(w.sectors().len(), 2);
}

#[test]
fn insertion_handles_missing_final_newline_and_empty_section() {
    for data in [b"[Sector.List]".as_slice(), b"[Sector.List]\nOld=true"] {
        let mut w = WorldData::read(data).unwrap();
        w.set_sector("New", true).unwrap();
        assert!(w.write().starts_with(data));
        assert!(w.write().ends_with(b"\nNew=true\n"));
    }
}

#[test]
fn removal_keeps_all_other_lines_exactly() {
    let mut w = WorldData::read(b"[Common]\n; header\n[Sector.List]\nA=true\n B=false ; gone\nC=true").unwrap();
    assert!(w.remove_sector("b").unwrap());
    assert_eq!(w.write(), b"[Common]\n; header\n[Sector.List]\nA=true\nC=true");
    assert!(!w.remove_sector("b").unwrap());
}

#[test]
fn malformed_or_ambiguous_lists_are_refused() {
    for data in [
        b"[Common]\nA=true".as_slice(), b"[Sector.List\nA=true", b"[Sector.List]\nA=true\na=false",
        b"[Sector.List]\n[Sector.List]", b"[Sector.List]\nA=perhaps", b"[Sector.List]\nA",
        b"[Sector.List]\n=true", b"\xFF\xFE[\0S\0", b"[Sector.List]\nA=true\0",
    ] { assert!(WorldData::read(data).is_err(), "accepted {data:?}"); }
}

#[test]
fn invalid_names_cannot_inject_lines_paths_or_extensions() {
    let mut w = WorldData::read(b"[Sector.List]\n").unwrap();
    for name in ["", "../Else", "Layer.sec", "A\nB", "A=true", "x/y", "x\\y"] {
        assert!(w.set_sector(name, true).is_err());
    }
    assert_eq!(w.write(), b"[Sector.List]\n");
}
