//! Lossless world sector lists (.wrldatasc). Keep bytes, comments, ordering and line endings;
//! only the requested value / entry is changed. Shipped lists are ASCII; single-byte comments
//! are preserved, UTF-8 BOM is accepted, UTF-16 / embedded NUL is refused.
use std::ops::Range;
use crate::bin_io::{ParseError, Result};

#[derive(Debug, Clone)]
pub struct SectorEntry {
    pub name: String,
    pub enabled: bool,
    line: Range<usize>,
    value: Range<usize>,
}

#[derive(Debug, Clone)]
pub struct WorldData {
    data: Vec<u8>,
    sectors: Vec<SectorEntry>,
    section_end: usize,
}

fn trim(data: &[u8], mut range: Range<usize>) -> Range<usize> {
    while range.start < range.end && data[range.start].is_ascii_whitespace() { range.start += 1; }
    while range.start < range.end && data[range.end - 1].is_ascii_whitespace() { range.end -= 1; }
    range
}

fn err<T>(offset: usize, msg: impl Into<String>) -> Result<T> {
    Err(ParseError { offset, msg: msg.into() })
}

impl WorldData {
    pub fn read(data: &[u8]) -> Result<Self> {
        if data.starts_with(&[0xFF, 0xFE]) || data.starts_with(&[0xFE, 0xFF]) || data.contains(&0) {
            return err(0, "UTF-16 / embedded NUL in world sector list is unsupported");
        }
        let mut sectors: Vec<SectorEntry> = Vec::new();
        let mut inside = false;
        let mut found = false;
        let mut section_end = data.len();
        let mut start = if data.starts_with(&[0xEF, 0xBB, 0xBF]) { 3 } else { 0 };
        while start < data.len() {
            let end = data[start..].iter().position(|&b| b == b'\n').map_or(data.len(), |n| start + n + 1);
            let content = trim(data, start..end);
            let comment = data[content.clone()].iter().position(|&b| b == b';' || b == b'#');
            let text = trim(data, content.start..comment.map_or(content.end, |n| content.start + n));
            let line = &data[text.clone()];
            if line.starts_with(b"[") {
                if !line.ends_with(b"]") { return err(start, "unclosed world-data section header"); }
                if inside { section_end = start; }
                inside = line.eq_ignore_ascii_case(b"[Sector.List]");
                if inside {
                    if found { return err(start, "duplicate [Sector.List]"); }
                    found = true;
                }
            } else if inside && !line.is_empty() {
                let Some(eq) = line.iter().position(|&b| b == b'=') else {
                    return err(start, "expected sector=true/false");
                };
                let key = trim(data, text.start..text.start + eq);
                let value = trim(data, text.start + eq + 1..text.end);
                if key.is_empty() { return err(start, "empty sector name"); }
                let name: String = data[key].iter().map(|&b| b as char).collect();
                if sectors.iter().any(|s| s.name.eq_ignore_ascii_case(&name)) {
                    return err(start, format!("duplicate sector {name}"));
                }
                let v = &data[value.clone()];
                let enabled = if v.eq_ignore_ascii_case(b"true") || v == b"1" { true }
                    else if v.eq_ignore_ascii_case(b"false") || v == b"0" { false }
                    else { return err(value.start, format!("invalid sector flag for {name}")); };
                sectors.push(SectorEntry { name, enabled, line: start..end, value });
            }
            start = end;
        }
        if !found { return err(0, "missing [Sector.List]"); }
        Ok(Self { data: data.to_vec(), sectors, section_end })
    }

    pub fn write(&self) -> Vec<u8> { self.data.clone() }
    pub fn sectors(&self) -> &[SectorEntry] { &self.sectors }

    /// Add or update a basename without .sec. Existing spelling and surrounding text stay intact.
    pub fn set_sector(&mut self, name: &str, enabled: bool) -> Result<()> {
        if name.is_empty() || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b"_{}-".contains(&b)) {
            return err(0, "invalid sector basename");
        }
        let mut data = self.data.clone();
        let flag = if enabled { "true" } else { "false" };
        if let Some(s) = self.sectors.iter().find(|s| s.name.eq_ignore_ascii_case(name)) {
            if s.enabled == enabled { return Ok(()); }
            data.splice(s.value.clone(), flag.bytes());
        } else {
            let newline = if data.windows(2).any(|w| w == b"\r\n") { "\r\n" } else { "\n" };
            let prefix = if self.section_end > 0 && data[self.section_end - 1] != b'\n' { newline } else { "" };
            let entry = format!("{prefix}{name}={flag}{newline}");
            data.splice(self.section_end..self.section_end, entry.bytes());
        }
        *self = Self::read(&data)?;
        Ok(())
    }

    pub fn remove_sector(&mut self, name: &str) -> Result<bool> {
        let Some(s) = self.sectors.iter().find(|s| s.name.eq_ignore_ascii_case(name)) else { return Ok(false) };
        let mut data = self.data.clone();
        data.drain(s.line.clone());
        *self = Self::read(&data)?;
        Ok(true)
    }
}
