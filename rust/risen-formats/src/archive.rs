//! "GENOMFLE" archive container used by .lrent, .sec, .wrl, .tple, ...
//!
//! ```text
//! "GENOMFLE", u16 version (1), u32 string table offset (= 14 + body length)
//! body (property objects; every string in it is a u16 index into the table)
//! string table: u32 0xDEADBEEF, u8 version (1), u32 count, count x (u16 length, bytes)
//! ```
//! The table is kept exactly as read (order included), so an unchanged file is written back byte for byte.
//! The Risen Editor rebuilt the table in first-use order on save.

use crate::bin_io::{ParseError, Reader, Result, Writer};

pub const MAGIC: &[u8; 8] = b"GENOMFLE";
pub const TABLE_MAGIC: u32 = 0xDEAD_BEEF;

/// Index into [`Archive::strings`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct StrRef(pub u16);

impl StrRef {
    pub fn read(r: &mut Reader) -> Result<StrRef> {
        Ok(StrRef(r.u16()?))
    }

    pub fn write(self, w: &mut Writer) {
        w.u16(self.0);
    }
}

#[derive(Debug, Clone)]
pub struct Archive {
    pub version: u16,
    pub body: Vec<u8>,
    pub table_version: u8,
    /// Latin-1 decoded (one char per byte), so writing back is lossless.
    pub strings: Vec<String>,
}

impl Archive {
    pub fn read(data: &[u8]) -> Result<Archive> {
        let mut r = Reader::new(data);
        if r.bytes(8)? != MAGIC {
            return r.err("not a GENOMFLE archive");
        }
        let version = r.u16()?;
        let table_offset = r.u32()? as usize;
        if table_offset < 14 || table_offset > data.len() {
            return r.err(format!("string table offset {table_offset} out of range"));
        }
        let body = data[14..table_offset].to_vec();
        let mut r = Reader::new(&data[table_offset..]);
        if r.u32()? != TABLE_MAGIC {
            return Err(ParseError { offset: table_offset, msg: "bad string table magic".into() });
        }
        let table_version = r.u8()?;
        let count = r.u32()? as usize;
        let mut strings = Vec::with_capacity(count);
        for _ in 0..count {
            let len = r.u16()? as usize;
            strings.push(r.bytes(len)?.iter().map(|&b| b as char).collect());
        }
        if r.remaining() != 0 {
            return r.err(format!("{} bytes after the string table", r.remaining()));
        }
        Ok(Archive { version, body, table_version, strings })
    }

    pub fn write(&self) -> Vec<u8> {
        let mut w = Writer::default();
        w.bytes(MAGIC);
        w.u16(self.version);
        w.u32(14 + self.body.len() as u32);
        w.bytes(&self.body);
        w.u32(TABLE_MAGIC);
        w.u8(self.table_version);
        w.u32(self.strings.len() as u32);
        for s in &self.strings {
            let bytes: Vec<u8> = s.chars().map(|c| c as u8).collect();
            w.u16(bytes.len() as u16);
            w.bytes(&bytes);
        }
        w.buf
    }

    pub fn str(&self, s: StrRef) -> &str {
        self.strings.get(s.0 as usize).map_or("<bad string index>", |s| s.as_str())
    }

    /// Index of `s`, appending it to the table if missing.
    pub fn intern(&mut self, s: &str) -> StrRef {
        match self.strings.iter().position(|x| x == s) {
            Some(i) => StrRef(i as u16),
            None => {
                self.strings.push(s.to_string());
                StrRef((self.strings.len() - 1) as u16)
            }
        }
    }
}
