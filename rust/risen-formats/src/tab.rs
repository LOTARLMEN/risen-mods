//! Localized string tables (`.tab`, data\compiled\strings.pak + data\common\strings.pak).
//!
//! Layout (tabFile.cs): "TAB0", u16 version (1), u16 format (1), u64 FILETIME, u32 column count, columns =
//! u8 present (always 1 in the shipped files), u16 column version (1), name, u32 row count, rows; a string is
//! u16 UTF-16 unit count + UTF-16LE. Column 0 holds the keys ("ID" / "Key"), text columns are "<Language>_Text"
//! (German, English, French, Italian, Spanish, Polish, Russian; compiled only has Polish + Russian); svms /
//! infos also have StageDir, WaveFile, Voice, ... columns. Items look their name up as FO_<template> in
//! focus.tab and the description as ITEMDESC_<template> in items.tab.

use crate::bin_io::{Reader, Result, Writer};

pub const MAGIC: &[u8; 4] = b"TAB0";

#[derive(Debug, Clone)]
pub struct Tab {
    pub version: u16,
    pub format: u16,
    pub filetime: u64,
    pub columns: Vec<Column>,
}

#[derive(Debug, Clone)]
pub struct Column {
    pub version: u16,
    pub name: String,
    pub rows: Vec<String>,
}

fn read_str(r: &mut Reader) -> Result<String> {
    let n = r.u16()? as usize;
    let at = r.pos();
    let units: Vec<u16> = r.bytes(n * 2)?.as_chunks::<2>().0.iter().map(|&c| u16::from_le_bytes(c)).collect();
    String::from_utf16(&units).or_else(|_| r.err(format!("string at 0x{at:X} is not valid UTF-16")))
}

fn write_str(w: &mut Writer, s: &str) {
    let units: Vec<u16> = s.encode_utf16().collect();
    w.u16(units.len() as u16);
    for u in units {
        w.u16(u);
    }
}

/// Short language codes for the text columns.
const LANGS: [(&str, &str); 7] = [
    ("de", "German"),
    ("en", "English"),
    ("fr", "French"),
    ("it", "Italian"),
    ("es", "Spanish"),
    ("pl", "Polish"),
    ("ru", "Russian"),
];

impl Tab {
    pub fn read(data: &[u8]) -> Result<Tab> {
        let mut r = Reader::new(data);
        if r.array::<4>()? != *MAGIC {
            return r.err("not a TAB0 file");
        }
        let version = r.u16()?;
        let format = r.u16()?;
        let filetime = r.u64()?;
        let count = r.u32()?;
        let mut columns = Vec::new();
        for _ in 0..count {
            // An absent column (0) never occurs in the shipped files; its layout is unknown.
            if r.u8()? != 1 {
                return r.err("column flag is not 1");
            }
            let version = r.u16()?;
            let name = read_str(&mut r)?;
            let n = r.u32()?;
            let rows = (0..n).map(|_| read_str(&mut r)).collect::<Result<Vec<_>>>()?;
            columns.push(Column { version, name, rows });
        }
        if r.remaining() != 0 {
            return r.err(format!("{} bytes after the last column", r.remaining()));
        }
        let rows = columns.first().map_or(0, |c| c.rows.len());
        if let Some(c) = columns.iter().find(|c| c.rows.len() != rows) {
            return r.err(format!("column {} has {} rows, the key column {rows}", c.name, c.rows.len()));
        }
        Ok(Tab { version, format, filetime, columns })
    }

    pub fn write(&self) -> Vec<u8> {
        let mut w = Writer::default();
        w.bytes(MAGIC);
        w.u16(self.version);
        w.u16(self.format);
        w.u64(self.filetime);
        w.u32(self.columns.len() as u32);
        for c in &self.columns {
            w.u8(1);
            w.u16(c.version);
            write_str(&mut w, &c.name);
            w.u32(c.rows.len() as u32);
            for s in &c.rows {
                write_str(&mut w, s);
            }
        }
        w.buf
    }

    pub fn rows(&self) -> usize {
        self.columns.first().map_or(0, |c| c.rows.len())
    }

    /// Row of a key (column 0): exact match first, then case-insensitive (the game's own matching is not known).
    pub fn find(&self, key: &str) -> Option<usize> {
        let keys = &self.columns.first()?.rows;
        keys.iter().position(|k| k == key).or_else(|| keys.iter().position(|k| k.eq_ignore_ascii_case(key)))
    }

    /// Column index for a column name (case-insensitive) or a language code (ru -> Russian_Text).
    pub fn column(&self, spec: &str) -> Option<usize> {
        let name = LANGS
            .iter()
            .find(|(code, _)| code.eq_ignore_ascii_case(spec))
            .map_or_else(|| spec.to_string(), |(_, lang)| format!("{lang}_Text"));
        self.columns.iter().position(|c| c.name.eq_ignore_ascii_case(&name))
    }

    /// The "<Language>_Text" columns (tutorial / achievements spell some "_text").
    pub fn text_columns(&self) -> Vec<usize> {
        (0..self.columns.len()).filter(|&i| self.columns[i].name.to_lowercase().ends_with("_text")).collect()
    }

    /// Row of `key`, appended (all other cells empty) when missing. Returns (row, added).
    pub fn row_or_add(&mut self, key: &str) -> (usize, bool) {
        if let Some(i) = self.find(key) {
            return (i, false);
        }
        for (i, c) in self.columns.iter_mut().enumerate() {
            c.rows.push(if i == 0 { key.to_string() } else { String::new() });
        }
        (self.rows() - 1, true)
    }

    /// Removes the row of `key`; false when there is none.
    pub fn remove(&mut self, key: &str) -> bool {
        let Some(i) = self.find(key) else { return false };
        for c in &mut self.columns {
            c.rows.remove(i);
        }
        true
    }
}
