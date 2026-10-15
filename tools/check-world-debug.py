"""Check the developer mod's resolved API against the actual installed x64 PE exports."""
import pathlib, re, runpy
root = pathlib.Path(__file__).resolve().parent.parent
pe_type = runpy.run_path(str(root / 'tools/pe_disasm.py'))['PE']
source = (root / 'mods/world-debug/main.cpp').read_text(encoding='utf-8')
modules = {'script': 'Script.dll', 'engine': 'Engine.dll', 'game': 'Game.dll', 'base': 'SharedBase.dll'}
exports = {name: set(pe_type(str(root.parent / 'bin' / path)).exports.values()) for name, path in modules.items()}
count = 0
for module, symbol in re.findall(r'API\(\w+,(\w+),"([^"]+)"\)', source):
    if symbol not in exports[module]:
        raise SystemExit(f'MISSING: {modules[module]}!{symbol}')
    count += 1
assert count >= 30
assert 'NUMPAD8' not in source, 'Conflicts with the native debug free-camera key'
print(f'{count} API exports found in installed game DLLs; native NUMPAD8 conflict avoided')
