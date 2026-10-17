"""Check the developer mod's resolved API against the actual installed x64 PE exports."""
import pathlib, re, runpy
root = pathlib.Path(__file__).resolve().parent.parent
pe_type = runpy.run_path(str(root / 'tools/pe_disasm.py'))['PE']
source = (root / 'mods/world-debug/main.cpp').read_text(encoding='utf-8')
modules = {'script': 'Script.dll', 'engine': 'Engine.dll', 'game': 'Game.dll', 'base': 'SharedBase.dll'}
exports = {name: set(pe_type(str(root.parent / 'bin' / path)).exports.values()) for name, path in modules.items()}
count = 0
for module, symbol in re.findall(r'(?:LIGHT_)?API\(\w+,(\w+),"([^"]+)"\)', source):
    if symbol not in exports[module]:
        raise SystemExit(f'MISSING: {modules[module]}!{symbol}')
    count += 1
assert count >= 30
assert 'NUMPAD8' not in source, 'Conflicts with the native debug free-camera key'
print(f'{count} API exports found in installed game DLLs; native NUMPAD8 conflict avoided')

# Retail GetDebugMode is not the freefly state: it is a constant-false stub.
# Inspect the installed binary independently of the mod's camera tracking and tests.
import struct
game = pe_type(str(root.parent / 'bin/Game.dll'))
engine = pe_type(str(root.parent / 'bin/Engine.dll'))
def code(rva, size):
    offset = game.off(rva)
    assert offset is not None
    return game.d[offset:offset + size]
for symbol,field in [('SetDay',0x2c),('SetHour',0x30),('SetMinute',0x34)]:
    rva=next(r for r,s in game.exports.items() if s.startswith('?'+symbol+'@gCClock_PS@@'))
    assert code(rva+0x2c,3)==bytes([0x89,0x47,field]), 'Clock property layout changed'
print('Retail clock day/hour/minute property stores verified')
weather_find=next(r for r,s in engine.exports.items() if s.startswith('?GetWeatherStatesByName@'))
offset=engine.off(weather_find+0x3e)
assert engine.d[offset:offset+4]==bytes.fromhex('48 83 c1 78'), 'Weather name layout changed'
print('Retail weather state name lookup field verified')
assert code(0x7a8230, 3) == bytes.fromhex('32 c0 c3'), 'GetDebugMode stub changed'
assert code(0x7ac510, 5) == bytes.fromhex('0f b6 41 38 c3'), 'Real freefly getter changed'
assert code(0x7ac558, 6) == bytes.fromhex('0f 94 c0 88 43 38'), 'Freefly toggle no longer writes flag +38'
assert code(0x7af5fe, 2) == bytes.fromhex('ff 15')
slot = 0x7af604 + struct.unpack('<i', code(0x7af600, 4))[0]
assert game.iat[slot] == 'Engine.dll!?RotateAngularRel@eCCameraBase@@QEAAXAEBVbCVector@@@Z'
assert '?RotateAngularRel@eCCameraBase@@QEAAXAEBVbCVector@@@Z' in engine.exports.values()
for rva, constant in [(0x7ac043, 0xf631a0), (0x7ac071, 0xf63194)]:
    assert code(rva, 4) == bytes.fromhex('f3 0f 59 05')
    assert rva + 8 + struct.unpack('<i', code(rva + 4, 4))[0] == constant
assert abs(struct.unpack('<f', code(0xf631a0, 4))[0] - .075) < 1e-8
assert abs(struct.unpack('<f', code(0xf63194, 4))[0] - .025) < 1e-8
assert code(0x7bdf4b, 4) == bytes.fromhex('f3 0f 59 30'), 'Normal camera radians conversion changed'
print('Retail stub/real freefly flag, rotation IAT call, input gains and radians conversion verified')
