"""Probe mounted-file priority in a separate process; no game files or game process touched."""
import ctypes as c
import os
from pathlib import Path
import struct

repo = Path(__file__).resolve().parents[1]
root = repo.parent
stage = repo / 'build/tests/vfs-native'
stage.mkdir(parents=True, exist_ok=True)

def pak(path, size, modified):
    name = b'probe.txt'
    data = b'x' * size
    text = lambda b: struct.pack('<I', len(b)) + b + b'\0'
    directory = struct.pack('<I', 0) + struct.pack('<QQQII', 0, 0, 0, 16, 1)
    directory += struct.pack('<I', 0) + text(name)
    directory += struct.pack('<QQQQIIIII', 48, 0, 0, modified, 0, 0, 0, size, size)
    total = 48 + len(data) + len(directory)
    path.write_bytes(struct.pack('<I4sQIIQQQ', 1, b'G3V0', 0, 0, 0, 48, 48 + len(data), total) + data + directory)

# Preload the system version.dll so SharedBase cannot load our proxy and its mods.
system_version = c.WinDLL(str(Path(os.environ['WINDIR']) / 'System32/version.dll'))
binary = root / 'bin'
shared = c.WinDLL(str(binary / 'SharedBase.dll'), winmode=0x1100)
ffc = c.WinDLL(str(binary / 'FFC.dll'), winmode=0x1100)
filesystem = c.WinDLL(str(binary / 'FileSystem2.dll'), winmode=0x1100)

def fn(dll, name, result, *args):
    f = getattr(dll, name)
    f.restype, f.argtypes = result, args
    return f

ptr = c.c_void_p
ctor = fn(shared, '??0bCString@@QEAA@PEBD@Z', ptr, ptr, c.c_char_p)
dtor = fn(shared, '??1bCString@@QEAA@XZ', None, ptr)
def string(text):
    value = c.c_uint64()
    ctor(c.byref(value), text.encode('ascii'))
    return value

get = fn(ffc, '?GetInstance@CFXVirtualFileSystem@@SAAEAV1@XZ', ptr)
create = fn(ffc, '?CreateVolume@CFXVirtualFileSystem@@QEAA_NAEBVbCString@@@Z', c.c_bool, ptr, ptr)
mount = fn(ffc, '?MountPackFile@CFXVirtualFileSystem@@QEAA_NAEBVbCString@@0K@Z', c.c_bool, ptr, ptr, ptr, c.c_uint32)
chain = fn(ffc, '?MountPackFileChain@CFXVirtualFileSystem@@QEAA_NAEBVbCString@@0K@Z', c.c_bool, ptr, ptr, ptr, c.c_uint32)
directory = fn(ffc, '?MountDirectory@CFXVirtualFileSystem@@QEAA_NAEBVbCString@@0K@Z', c.c_bool, ptr, ptr, ptr, c.c_uint32)
size = fn(ffc, '?GetFileSize@CFXVirtualFileSystem@@QEBA_KAEBVbCString@@@Z', c.c_uint64, ptr, ptr)
mkdir = fn(ffc, '?MakeDir@CFXVirtualFileSystem@@QEAA_NAEBVbCString@@@Z', c.c_bool, ptr, ptr)
cache = fn(ffc, '?EnableCache@CFXVirtualFileSystem@@QEAAX_N@Z', None, ptr, c.c_bool)
vfs = get()
print('VFS', hex(vfs), 'system', hex(c.c_void_p.from_address(vfs).value or 0), flush=True)
volume = string('TEST')
assert create(vfs, c.byref(volume)), 'CreateVolume failed'

def check(label, first_time, second_time, loose=False, use_chain=False):
    folder = stage / label
    folder.mkdir(exist_ok=True)
    base = folder / 'asset.pak'
    other = folder / ('asset.p00' if use_chain else 'other.pak')
    pak(base, 11, first_time)
    pak(other, 22, second_time)
    if use_chain:
        assert not (folder / 'asset.p01').exists(), 'Fixture gap must be empty'
        pak(folder / 'asset.p02', 44, 300)  # should not load across the .p01 gap
    path = string('#TEST:/' + label + '/')
    mkdir(vfs, c.byref(path))
    a, b = string(str(base)), string(str(other))
    if loose:
        physical = folder / 'loose'
        physical.mkdir(exist_ok=True)
        (physical / 'probe.txt').write_bytes(b'z' * 33)
        p = string(str(physical))
        assert directory(vfs, c.byref(path), c.byref(p), 0), 'MountDirectory failed'
    assert (chain if use_chain else mount)(vfs, c.byref(path), c.byref(a), 1), 'First mount failed'
    if not use_chain:
        assert mount(vfs, c.byref(path), c.byref(b), 1), 'Second mount failed'
    query = string('#TEST:/' + label + '/probe.txt')
    actual = size(vfs, c.byref(query))
    assert actual == (33 if loose else 22), (label, actual)
    print(label, 'PASS, effective file size', actual, flush=True)

check('newer', 100, 200)
check('older', 200, 100)
check('equal', 200, 200)
check('loose', 200, 100, loose=True)
check('chain_older', 200, 100, use_chain=True)
check('chain_equal', 200, 200, use_chain=True)
cache(vfs, True)
print('CACHE ENABLED', flush=True)
for label in ['newer', 'older', 'equal', 'loose', 'chain_older', 'chain_equal']:
    query = string('#TEST:/' + label + '/probe.txt')
    actual = size(vfs, c.byref(query))
    assert actual == (33 if label == 'loose' else 22), (label, actual)
    print(label, 'PASS with cache, file size', actual, flush=True)
