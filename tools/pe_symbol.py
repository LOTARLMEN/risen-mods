"""Resolve module+rva addresses to the nearest preceding export.
Usage: python -I pe_symbol.py <dir-or-dll>... -- Module.dll+0x1234 ...
Modules are looked up in the given dirs (and System32)."""
import os, sys
import pefile

def main():
    args = sys.argv[1:]
    sep = args.index('--')
    dirs = args[:sep] + [r'C:\Windows\System32']
    cache = {}
    for item in args[sep + 1:]:
        mod, rva = item.split('+')
        rva = int(rva, 16)
        if mod.lower() not in cache:
            path = next((os.path.join(d, mod) for d in dirs if os.path.isfile(os.path.join(d, mod))), None)
            exports = []
            if path:
                pe = pefile.PE(path, fast_load=True)
                pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
                if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
                    exports = sorted((s.address, (s.name or b'#%d' % s.ordinal).decode()) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols)
            cache[mod.lower()] = exports
        best = None
        for addr, name in cache[mod.lower()]:
            if addr <= rva:
                best = (addr, name)
            else:
                break
        print(f'{item:32} {best[1]}+0x{rva - best[0]:x}' if best else f'{item:32} ?')

main()
