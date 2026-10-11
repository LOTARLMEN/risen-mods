"""Disassemble one function of an x64 PE via MSVC dumpbin and annotate it:
IAT calls -> imported (demangled) names, direct calls/jumps -> local exports,
rip-relative data -> exported data names or string literals.
Stops at the first int3 padding (function end) or after max-lines. Needs dumpbin.exe (VS Build Tools) and dbghelp.
Usage: python -I pe_disasm.py file.dll <export-substring | 0xRVA> [max-lines]"""
import ctypes, glob, re, struct, subprocess, sys

DUMPBIN = (glob.glob(r'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') or ['dumpbin'])[-1]
_und = ctypes.WinDLL('dbghelp.dll').UnDecorateSymbolName
_und.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]

def demangle(name):
    if not name.startswith('?'):
        return name
    buf = ctypes.create_string_buffer(4096)
    n = _und(name.encode('latin1'), buf, 4096, 0x1000)  # UNDNAME_NAME_ONLY
    short = buf.value[:n].decode('latin1') if n else name
    if '::' not in short and name.count('@') > 2:  # data member: name-only drops the class
        n = _und(name.encode('latin1'), buf, 4096, 0)
        short = buf.value[:n].decode('latin1').replace(' __ptr64', '') if n else name
    return short

class PE:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from('<Q', d, opt + 24)[0]
        dd = opt + 112
        self.secs = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]
        self.dirs = [struct.unpack_from('<II', d, dd + 8 * i) for i in range(16)]
        self.exports = {}
        er, es = self.dirs[0]
        if er:
            e = self.off(er)
            _, nf, nn, af, an, ao = struct.unpack_from('<IIIIII', d, e + 16)
            funcs = struct.unpack_from('<%dI' % nf, d, self.off(af))
            for i in range(nn):
                nrva = struct.unpack_from('<I', d, self.off(an) + 4 * i)[0]
                o = struct.unpack_from('<H', d, self.off(ao) + 2 * i)[0]
                self.exports[funcs[o]] = self.cstr(self.off(nrva))
        self.iat = {}
        ir = self.dirs[1][0]
        o = self.off(ir) if ir else None
        while o is not None:
            olt, _, _, nm, ft = struct.unpack_from('<IIIII', d, o)
            if not nm:
                break
            dll = self.cstr(self.off(nm))
            t, i = self.off(olt or ft), 0
            while True:
                v = struct.unpack_from('<Q', d, t + 8 * i)[0]
                if not v:
                    break
                name = '#%d' % (v & 0xffff) if v >> 63 else self.cstr(self.off(v & 0x7fffffff) + 2)
                self.iat[ft + 8 * i] = dll + '!' + name
                i += 1
            o += 20

    def off(self, rva):
        for _, vs, va, rs, ro in self.secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro

    def cstr(self, o):
        return self.d[o:self.d.index(b'\0', o)].decode('latin1')

    def string_at(self, rva):
        o = self.off(rva)
        if o is None:
            return None
        s = self.d[o:o + 200]
        m = re.match(rb'([\x20-\x7e]{3,})\0', s)
        if m:
            return '"%s"' % m.group(1).decode()
        m = re.match(rb'((?:[\x20-\x7e]\0){3,})\0\0', s)
        if m:
            return 'L"%s"' % m.group(1).decode('utf-16le')
        return None

def main():
    path, what = sys.argv[1], sys.argv[2]
    maxl = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    pe = PE(path)
    if what.startswith('0x'):
        rva = int(what, 16)
    else:
        hits = [(r, n) for r, n in pe.exports.items() if what in n]
        if len(hits) != 1:
            for r, n in hits[:40]:
                print('%08x %s' % (r, n))
            sys.exit('%d matches' % len(hits))
        rva = hits[0][0]
    b, e = rva, rva + 0x4000
    print('; %s %08x %s' % (path.replace('\\', '/').split('/')[-1], b, demangle(pe.exports.get(b, ''))))
    out = subprocess.run([DUMPBIN, '/nologo', '/disasm:nobytes', '/range:0x%x,0x%x' % (pe.base + b, pe.base + e - 1), path],
                         capture_output=True, text=True).stdout
    n = 0
    for line in out.splitlines():
        m = re.match(r'\s+([0-9A-F]{16}): (.*)', line)
        if not m:
            continue
        text, notes = m.group(2), []
        if text.startswith('int         3'):
            break
        for a in re.findall(r'([0-9A-F]{16})h?\b', text):
            r = int(a, 16) - pe.base
            if r in pe.iat:
                notes.append(demangle(pe.iat[r].split('!', 1)[1]) + ' [' + pe.iat[r].split('!')[0] + ']')
            elif r in pe.exports:
                notes.append(demangle(pe.exports[r]))
            elif text.startswith('call') or (text.startswith('jmp') and r < b):
                notes.append('sub_%x' % r)
            else:
                s = pe.string_at(r)
                if s:
                    notes.append(s)
        print('%08x  %-60s%s' % (int(m.group(1), 16) - pe.base, text, ('; ' + ' | '.join(notes)) if notes else ''))
        n += 1
        if n >= maxl:
            break

if __name__ == '__main__':
    main()
