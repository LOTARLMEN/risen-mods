"""Print DLL imports of PE files (x64). Usage: python -I pe_imports.py file..."""
import struct, sys

def parse(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    dd = opt + (112 if struct.unpack_from('<H', d, opt)[0] == 0x20b else 96)
    secs = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]
    def off(rva):
        for _, vs, va, rs, ro in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
    def cstr(o):
        return d[o:d.index(b'\0', o)].decode('latin1')
    res = []
    for idx in (1, 13):  # import table, delay-import table
        rva = struct.unpack_from('<I', d, dd + 8 * idx)[0]
        if not rva:
            continue
        o = off(rva)
        step = 20 if idx == 1 else 32
        name_field = 12 if idx == 1 else 4
        while True:
            name_rva = struct.unpack_from('<I', d, o + name_field)[0]
            if not name_rva:
                break
            res.append(cstr(off(name_rva)) + (' (delay)' if idx == 13 else ''))
            o += step
    return res

for p in sys.argv[1:]:
    print(p.split('/')[-1] + ': ' + ', '.join(parse(p)))
