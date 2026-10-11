"""Print exports of PE files (x64) as 'RVA name' lines (forwarders shown as '-> target').
Usage: python -I pe_exports.py file.dll [filter-substring]"""
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
    exp_rva, exp_size = struct.unpack_from('<II', d, dd)
    if not exp_rva:
        return []
    e = off(exp_rva)
    base, nfunc, nnames, afunc, anames, aords = struct.unpack_from('<IIIIII', d, e + 16)
    funcs = struct.unpack_from('<%dI' % nfunc, d, off(afunc)) if nfunc else ()
    names = struct.unpack_from('<%dI' % nnames, d, off(anames)) if nnames else ()
    ords = struct.unpack_from('<%dH' % nnames, d, off(aords)) if nnames else ()
    named = {o: cstr(off(n)) for n, o in zip(names, ords)}
    res = []
    for i, rva in enumerate(funcs):
        if not rva:
            continue
        name = named.get(i, '#%d' % (base + i))
        if exp_rva <= rva < exp_rva + exp_size:
            name += ' -> ' + cstr(off(rva))
        res.append((rva, name))
    return sorted(res)

if __name__ == '__main__':
    flt = sys.argv[2].lower() if len(sys.argv) > 2 else None
    for rva, name in parse(sys.argv[1]):
        if not flt or flt in name.lower():
            print('%08x %s' % (rva, name))
