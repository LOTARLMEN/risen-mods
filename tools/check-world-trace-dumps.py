"""Independently verify exception context and selective heap bytes in real minidumps."""
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
pid = (root.parent / "run-pid.txt").read_text().strip()
files = list(root.glob(f"*-p{pid}-t*.dmp"))
assert files, "no minidumps"
found = 0
for path in files:
    data = path.read_bytes()
    signature, _, count, directory = struct.unpack_from("<4I", data)
    assert signature == 0x504D444D, path
    streams = {}
    for index in range(count):
        kind, size, rva = struct.unpack_from("<III", data, directory + index * 12)
        streams[kind] = (rva, size)
    assert 6 in streams and 3 in streams, "exception and thread streams required"
    rva, _ = streams[6]
    code = struct.unpack_from("<I", data, rva + 8)[0]
    assert code == 0xC0000005
    _, context_rva = struct.unpack_from("<II", data, rva + 160)
    rbx = struct.unpack_from("<Q", data, context_rva + 144)[0]
    if 5 not in streams:
        continue
    memory_rva, _ = streams[5]
    ranges = struct.unpack_from("<I", data, memory_rva)[0]
    for index in range(ranges):
        start, size, contents = struct.unpack_from("<QII", data, memory_rva + 4 + index * 16)
        if start <= rbx < start + size:
            offset = contents + rbx - start
            if data[offset:offset + 31] == b"DIAGNOSTIC_HEAP_MARKER_64F45D6C":
                found += 1
                assert path.with_suffix(".dmp.log").exists()
                break
assert found >= 3, f"expected ordinary, reserved physics and unhandled dumps with heap marker, got {found}"
print(f"PASS: {found} dumps preserve the exception RBX heap marker and independent logs")
