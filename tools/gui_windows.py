"""Dump the GUI layouts from compiled_windows.bin (GENOMFLE) as one line per window.
Get the file: python -I pak_list.py ../data/compiled/library.pak --extract compiled_windows.bin <out>
Usage: python -I gui_windows.py compiled_windows.bin [--raw]
  default: one line per window: name, ID (- = 0xFFFF), position, size, derived-class properties
  --raw:   every property record found (offset, name, type, value)
Heuristic parser: property records are [u16 name][u16 type][u16 version][u32 size][data], strings are u16 indexes
into the string table at the end (DEADBEEF, u8 1, u32 count, {u16 len, chars}). A window's derived-class properties
come BEFORE its base eCGuiWindow2 block (Name ... Size); a page is followed by its children. See notes/ui-recon.md."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'GENOMFLE', 'not a GENOMFLE file'
st = struct.unpack_from('<I', d, 10)[0]
assert struct.unpack_from('<I', d, st)[0] == 0xDEADBEEF
S, p = [], st + 9
for _ in range(struct.unpack_from('<I', d, st + 5)[0]):
    n = struct.unpack_from('<H', d, p)[0]
    S.append(d[p + 2:p + 2 + n].decode('latin1'))
    p += 2 + n

SIMPLE = ('bool', 'int', 'long', 'float', 'char', 'short', 'bCString', 'bCPoint', 'bCRect', 'eCLocString',
          'eCTipLocString', 'bCGuid', 'unsigned short', 'unsigned long', 'unsigned int', 'eCScriptProxyScript',
          'eCEntityStringProxy', 'eCGuiBitmapProxy2', 'bCByteAlphaColor', 'bCFloatColor', 'eCTemplateEntityProxy',
          'eCEntityProxy')
TYPES = {i for i, s in enumerate(S) if s in SIMPLE or s.startswith(('bTPropertyContainer', 'bTObjArray', 'bTValArray'))}
STRINGISH = ('bCString', 'eCLocString', 'eCTipLocString', 'eCScriptProxyScript', 'eCGuiBitmapProxy2', 'eCEntityStringProxy')


def value(t, b):
    if t == 'bool' and len(b) == 1:
        return str(bool(b[0]))
    if t in ('int', 'long') and len(b) == 4:
        return str(struct.unpack('<i', b)[0])
    if t in ('unsigned long', 'unsigned int') and len(b) == 4:
        return str(struct.unpack('<I', b)[0])
    if t in ('short', 'unsigned short') and len(b) == 2:
        v = struct.unpack('<H', b)[0]
        return '-' if v == 0xFFFF else str(v)
    if t == 'float' and len(b) == 4:
        return '%g' % struct.unpack('<f', b)[0]
    if t == 'bCPoint' and len(b) == 8:
        return '(%d,%d)' % struct.unpack('<ii', b)
    if t == 'bCRect' and len(b) == 16:
        return '(%d,%d,%d,%d)' % struct.unpack('<iiii', b)
    if t.startswith('bTPropertyContainer') and len(b) == 6:
        return 'enum %d' % struct.unpack_from('<I', b, 2)[0]
    if t in STRINGISH:
        o = {2: 0, 5: 3}.get(len(b))  # bCString = u16 index; script/entity proxy = u16 ver, u8, u16 index
        if o is None:
            return 'hex ' + b.hex()
        i = struct.unpack_from('<H', b, o)[0]
        return '"%s"' % (S[i] if i < len(S) else '?%d' % i)
    return b.hex() if len(b) <= 24 else '<%d bytes>' % len(b)


props = []
p = 0
while p + 10 <= st:
    ni, ti = struct.unpack_from('<HH', d, p)
    size = struct.unpack_from('<I', d, p + 6)[0]
    if ni < len(S) and ti in TYPES and size < 0x10000 and S[ni][:1].isalpha():
        props.append((p, S[ni], S[ti], value(S[ti], d[p + 10:p + 10 + size])))
        p += 10 + size
    else:
        p += 1

if '--raw' in sys.argv:
    for off, name, t, v in props:
        print('%08x %-28s %-24s %s' % (off, name, t[:24], v))
    sys.exit()

BASE = set('''Visible Transparent ToolTipText ToolTip Name KeyboardDisabled IDUp IDRight IDLeft IDDown FontColorPushed
FontColorNormal FontColorHot FontColorDisabled Font Dock DispatchID Disabled Cursor BorderExtent Border AutoSize AutoFocus
AnchorTop AnchorRight AnchorMode AnchorLeft AnchorBottom ID Position Size'''.split())
SKIP = set('''Caption SysMenu MinimizeBox MaximizeBox SizeBox HorizontalScrollBar VerticalScrollBar SizeCaption SizeIcon
SizeMinimize SizeRestore SizeMaximize SizeClose SndUnPush SndPush SndClick EnableDragDrop'''.split())
derived, base, inbase = [], {}, False


def flush():
    if base:
        print('%-28s ID=%-5s pos=%-11s size=%-10s %s' % (base.get('Name', '').strip('"'), base.get('ID', ''),
              base.get('Position', ''), base.get('Size', ''), ' '.join(derived)))


for off, name, t, v in props:
    if name in BASE:
        if not inbase:
            inbase, base = True, {}
        if name in ('Name', 'ID', 'Position', 'Size'):
            base[name] = v
        continue
    if inbase:
        flush()
        derived, base, inbase = [], {}, False
    if name not in SKIP and v not in ('False', '0', 'enum 0', '""', 'hex 010000'):
        derived.append('%s=%s' % (name, v))
flush()
