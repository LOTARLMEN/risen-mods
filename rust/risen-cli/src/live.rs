//! Client of the in-game live bridge (`mods/live-bridge`, dev-only live_bridge.dll).
//! Protocol: named pipe `\\.\pipe\RisenLive`, one JSON object per line each way, request/response only.
//! Request `{"cmd":"<verb>","args":["..",..]}`; the bridge parses the args of each verb itself.

use std::fs::{File, OpenOptions};
use std::io::{BufRead, BufReader, Write};
use std::time::{Duration, Instant};

pub const PIPE: &str = r"\\.\pipe\RisenLive";
const ERROR_FILE_NOT_FOUND: i32 = 2;
const ERROR_PIPE_BUSY: i32 = 231;

fn connect(wait: Duration) -> Result<File, String> {
    let start = Instant::now();
    loop {
        match OpenOptions::new().read(true).write(true).open(PIPE) {
            Ok(f) => return Ok(f),
            // One client at a time: another request is in flight, retry until `wait` runs out.
            Err(e) if e.raw_os_error() == Some(ERROR_PIPE_BUSY) && start.elapsed() < wait => {
                std::thread::sleep(Duration::from_millis(50))
            }
            Err(e) if e.raw_os_error() == Some(ERROR_FILE_NOT_FOUND) => {
                return Err("bridge not running: start the game with bin\\mods\\live_bridge.dll".into())
            }
            Err(e) => return Err(format!("{PIPE}: {e}")),
        }
    }
}

/// Sends one request line and returns the bridge's reply line (without the newline).
pub fn request(line: &str) -> Result<String, String> {
    if line.contains('\n') {
        return Err("request must be one line".into());
    }
    let mut pipe = connect(Duration::from_secs(5))?;
    pipe.write_all(format!("{line}\n").as_bytes()).map_err(|e| format!("write: {e}"))?;
    let mut reply = String::new();
    BufReader::new(pipe).read_line(&mut reply).map_err(|e| format!("read: {e}"))?;
    if reply.is_empty() {
        return Err("bridge closed the connection without a reply".into());
    }
    Ok(reply.trim_end().to_string())
}

pub fn command_line(verb: &str, args: &[String]) -> String {
    serde_json::json!({ "cmd": verb, "args": args }).to_string()
}
