"""List or extract files of a Risen G3V0 .pak (directory scanned heuristically, same layout as shared/game_assets.cpp).
Usage: python -I pak_list.py file.pak [regex]
       python -I pak_list.py file.pak --extract name outfile"""
import re, struct, sys, zlib

path = sys.argv[1]
d = open(path, 'rb').read()
assert d[4:8] == b'G3V0', 'not a G3V0 pak'
dr = d[struct.unpack_from('<Q', d, 32)[0]:]
entries = []  # (name, offset, size, encryption, compression)
i = 0
while i + 5 < len(dr):
    n = struct.unpack_from('<I', dr, i)[0]
    if 1 <= n < 260 and i + 4 + n < len(dr) and dr[i + 4 + n] == 0:
        s = dr[i + 4:i + 4 + n]
        p = i + 4 + n + 1
        if all(32 <= c < 127 for c in s) and p + 52 <= len(dr):
            off = struct.unpack_from('<Q', dr, p)[0]
            enc, comp, size = struct.unpack_from('<III', dr, p + 36)
            if off < len(d) and size < len(d) and enc < 8 and comp < 8:
                entries.append((s.decode(), off, size, enc, comp))
                i = p + 52
                continue
    i += 1

if len(sys.argv) > 4 and sys.argv[2] == '--extract':
    for name, off, size, enc, comp in entries:
        if name == sys.argv[3]:
            data = d[off:off + size]
            if comp:
                data = zlib.decompress(data)
            open(sys.argv[4], 'wb').write(data)
            print('wrote %d bytes (encryption %d, compression %d)' % (len(data), enc, comp))
    sys.exit()

flt = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
for name, off, size, enc, comp in entries:
    if not flt or flt.search(name):
        print('%10d %d%d %s' % (size, enc, comp, name))
print('total', len(entries), file=sys.stderr)
