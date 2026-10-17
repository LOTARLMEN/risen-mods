"""Scratch-only static asset feasibility probe, not a general mesh importer/editor.

Prepare: stage must contain compiled_meshes.original.bin and donor._xmsh extracted from VFS.
Finish: wraps the <name>.cooked streams made by mesh-cook-check into ._xcom resources.
Verify: independently compares native PhysX output triangles with the authored geometry.
All generated output stays under this repository's build/tests; never installs anything.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import time


def u16(n): return struct.pack('<H', n)
def u32(n): return struct.pack('<I', n)
def u64(n): return struct.pack('<Q', n)
def floats(v): return struct.pack('<' + 'f' * len(v), *v)
def text(s):
    b = s.encode('latin1')
    return u16(len(b)) + b


def property_bytes(name, ty, value, string=text):
    return string(name) + string(ty) + u16(30) + u32(len(value)) + value


def properties(items, string=text):
    return u16(201) + u32(len(items)) + b''.join(property_bytes(*p, string) for p in items)


def object_bytes(cls, items, class_data, string=text):
    payload = properties(items, string) + class_data
    return u16(1) + b'\1' + u16(1) + b'\1' + string(cls) + u16(1) + b'\0' + u16(201) * 2 + u32(len(payload)) + payload


def box(vertices):
    return [min(p[k] for p in vertices) for k in range(3)] + [max(p[k] for p in vertices) for k in range(3)]


def mesh_properties(vertices, faces, material, string=text):
    bounds = floats(box(vertices))
    sub = properties([
        ('MaterialName', 'bCString', string(material)),
        ('Extends', 'bCBox', bounds),
        ('FirstIndex', 'long', u32(0)),
        ('IndexCount', 'long', u32(len(faces) * 3)),
        ('FirstVertex', 'long', u32(0)),
        ('VertexCount', 'long', u32(len(vertices))),
    ], string) + u16(201) + u32(1) + bounds + u32(0) + u32(1)
    return [
        ('SubMeshes', 'bTObjArray<class eCSubMesh>', b'\1' + u32(1) + u16(201) + u32(len(sub)) + sub),
        ('LightmapName', 'bCString', string('')),
        ('Boundary', 'bCBox', bounds),
    ]


def resource(magic, cls, items, payload, stamp):
    header = object_bytes(cls, items, u16(201))
    offset = 40 + len(header)
    return magic + u32(40) + u32(len(header)) + u32(offset) + u32(len(payload)) + u64(stamp) + u64(0) + header + payload


def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def sub(a, b): return [a[k]-b[k] for k in range(3)]
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def unit(v):
    n = math.sqrt(dot(v, v))
    if not math.isfinite(n) or n < 1e-10: raise ValueError('degenerate face')
    return [x/n for x in v]


def xmsh(vertices, faces, material, stamp):
    # Flat shaded triangle soup: every corner has explicit normal, tangent, colors and UV.
    expanded, stream = [], bytearray()
    for face in faces:
        a, b, c = [vertices[i] for i in face]
        normal = unit(cross(sub(b, a), sub(c, a)))
        axis = [1., 0., 0.] if abs(normal[0]) < .8 else [0., 0., 1.]
        tangent = unit(sub(axis, [dot(axis, normal)*v for v in normal]))
        bitangent = cross(normal, tangent)
        for p in [a, b, c]:
            expanded.append(p)
            stream += floats(p + normal + tangent) + bytes([255]*4 + [0, 0, 0, 255])
            stream += floats([dot(p, tangent)/100., dot(p, bitangent)/100.])
    # Native XMSH uses the opposite winding to outward vertex normals (verified on shipped meshes).
    # PhysX cooking below keeps the authored winding: render and physics conventions differ.
    triangles = [(i+2, i+1, i) for i in range(0, len(expanded), 3)]
    declaration = b''.join(struct.pack('<HHBBBB', *d) for d in [
        (0, 0, 2, 0, 0, 0), (0, 12, 2, 0, 3, 0), (0, 24, 2, 0, 6, 0),
        (0, 36, 4, 0, 10, 0), (0, 40, 4, 0, 10, 1), (0, 44, 1, 0, 5, 0),
        (3, 0, 0, 0, 5, 4), (255, 0, 17, 0, 0, 0), (255, 0, 17, 0, 0, 0),
    ])
    declaration += bytes(128-len(declaration))
    payload = declaration + u32(len(stream)) + u32(0)*2 + u32(1) + u32(52) + bytes(40)
    payload += u32(len(triangles)*12) + u32(0) + u32(102) + u32(1) + stream
    payload += b''.join(u32(i) for f in triangles for i in f) + b'\1'
    return resource(b'GR01MS02', 'eCMeshResource2', mesh_properties(expanded, triangles, material), payload, stamp), expanded, triangles


def donor_material(data):
    # Strictly find the inline MaterialName property in the supplied native donor.
    prefix = text('MaterialName') + text('bCString') + u16(30)
    at = data.find(prefix)
    if at < 0 or data.find(prefix, at+1) >= 0: raise ValueError('expected single-material donor')
    at += len(prefix)
    size, length = struct.unpack_from('<IH', data, at)
    if size != length+2: raise ValueError('invalid donor material property')
    return data[at+6:at+6+length].decode('latin1')


def collision_properties(bounds):
    return [('IsConvex', 'bool', b'\0'), ('Boundary', 'bCBox', floats(bounds)),
            ('SubBoundaries', 'bTObjArray<class bCBox>', b'\1'+u32(1)+floats(bounds))]


def append_cache(seed, records, material, stamp, collision=False):
    if seed[:10] != b'GENOMFLE\1\0': raise ValueError('unsupported cache archive')
    table = struct.unpack_from('<I', seed, 10)[0]
    body = bytearray(seed[14:table])
    if body[:4] != b'GH04': raise ValueError('unsupported cache revision')
    if seed[table:table+5] != b'\xef\xbe\xad\xde\1': raise ValueError('invalid string table')
    count = struct.unpack_from('<I', seed, table+5)[0]
    strings, at = [], table+9
    for _ in range(count):
        n = struct.unpack_from('<H', seed, at)[0]; at += 2
        strings.append(seed[at:at+n].decode('latin1')); at += n
    if at != len(seed): raise ValueError('trailing string data')
    def ref(s):
        if s not in strings: strings.append(s)
        if len(strings) > 65536: raise ValueError('string index overflow')
        return u16(strings.index(s))
    old_count = struct.unpack_from('<I', body, 4)[0]
    for name, vertices, faces in records:
        if any(name.casefold() == s.casefold() for s in strings): raise ValueError('resource name already exists')
        items = collision_properties(box(vertices)) if collision else mesh_properties(vertices, faces, material, ref)
        cls = 'eCCollisionMeshResource2' if collision else 'eCMeshResource2'
        obj = object_bytes(cls, items, u16(201), ref)
        body += (b'CM00' if collision else b'MS02') + obj + ref(name) + u32(0) + u64(stamp)*2
    struct.pack_into('<I', body, 4, old_count+len(records))
    assert body[8:len(seed[14:table])] == seed[22:table], 'old cache entries changed'
    return b'GENOMFLE\1\0' + u32(14+len(body)) + body + b'\xef\xbe\xad\xde\1' + u32(len(strings)) + b''.join(text(s) for s in strings)


def shapes():
    # Closed triangular prism. Orient each triangle outwards using its centroid.
    vertices = [[-100., 0., 0.], [100., 0., 0.], [-100., 0., 200.], [100., 0., 200.], [-100., 80., 200.], [100., 80., 200.]]
    faces = [[0, 1, 2], [1, 3, 2], [0, 4, 1], [1, 4, 5], [2, 3, 4], [3, 5, 4], [0, 2, 4], [1, 5, 3]]
    center = [sum(p[k] for p in vertices)/len(vertices) for k in range(3)]
    for f in faces:
        a, b, c = [vertices[i] for i in f]
        if dot(cross(sub(b, a), sub(c, a)), sub([(a[k]+b[k]+c[k])/3 for k in range(3)], center)) < 0: f.reverse()
    yield 'RisenMods_Probe_Ramp', vertices, faces
    yield 'RisenMods_Probe_Stretch', [[p[0]*1.5, p[1]*.5, p[2]] for p in vertices], faces
    # Shorten the existing wedge by clipping z <= 100 and adding the closing rectangle.
    yield 'RisenMods_Probe_Trim', [[p[0], p[1]*.5, p[2]*.5] for p in vertices], faces
    grid = [[float(x), 35. if x == 0 and z == 0 else 0., float(z)] for z in [-100, 0, 100] for x in [-100, 0, 100]]
    tiles = []
    for z in range(2):
        for x in range(2):
            a = z*3+x; tiles.extend([[a, a+3, a+1], [a+1, a+3, a+4]])
    yield 'RisenMods_Probe_Terrain', grid, tiles


def write_new(path, data):
    with path.open('xb') as out: out.write(data)


def fix_winding(data):
    at = struct.unpack_from('<I', data, 16)[0]
    if data[:8] != b'GR01MS02': raise ValueError('not a static render mesh')
    nbytes = struct.unpack_from('<I', data, at+128)[0]
    stride = struct.unpack_from('<I', data, at+144)[0]
    ibytes = struct.unpack_from('<I', data, at+188)[0]
    if stride != 52 or ibytes % 12 or nbytes % stride: raise ValueError('unexpected probe vertex/index layout')
    vb = at+204
    if vb+nbytes+ibytes+1 != len(data): raise ValueError('unexpected probe resource size')
    out = bytearray(data)
    for i in range(ibytes//12):
        index_at = vb+nbytes+i*12
        indices = struct.unpack_from('<III', data, index_at)
        if max(indices) >= nbytes//stride: raise ValueError('invalid index')
        p = [list(struct.unpack_from('<fff', data, vb+j*stride)) for j in indices]
        n = [list(struct.unpack_from('<fff', data, vb+j*stride+12)) for j in indices]
        geometric = unit(cross(sub(p[1],p[0]),sub(p[2],p[0])))
        normal = unit([sum(v[k] for v in n) for k in range(3)])
        if dot(geometric,normal) < .99: raise ValueError('expected the previously inverted probe winding')
        struct.pack_into('<III', out, index_at, *reversed(indices))
    # Header/materials/bounds, all vertex attributes and geometry remain byte-identical.
    assert out[:vb+nbytes] == data[:vb+nbytes] and out[vb+nbytes+ibytes:] == data[vb+nbytes+ibytes:]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'finish', 'verify', 'collision-cache', 'fix-winding'])
    parser.add_argument('stage', type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve(strict=True)
    allowed = Path(__file__).resolve().parents[1] / 'build' / 'tests'
    if not stage.is_relative_to(allowed.resolve()) or stage == allowed.resolve(): raise ValueError('stage must be a child of build/tests')
    if args.mode == 'prepare':
        material = donor_material((stage/'donor._xmsh').read_bytes())
        stamp = int((time.time()+11644473600)*10000000)
        records, manifest = [], {'material': material, 'filetime': stamp, 'assets': []}
        for name, vertices, faces in shapes():
            data, expanded, triangles = xmsh(vertices, faces, material, stamp)
            write_new(stage/(name+'._xmsh'), data)
            records.append((name, expanded, triangles))
            write_new(stage/(name+'.input'), b'MESHCHK1'+u32(len(vertices))+u32(len(faces))+b''.join(floats([v/100. for v in p]) for p in vertices)+b''.join(u32(i) for f in faces for i in f))
            obj = '\n'.join(['# Native centimetres, Y up', 'usemtl '+material.removesuffix('._xmat')]+['v '+' '.join(map(str,p)) for p in vertices]+['f '+' '.join(str(i+1) for i in f) for f in faces])+'\n'
            write_new(stage/(name+'.obj'), obj.encode('ascii'))
            manifest['assets'].append({'name': name, 'vertices': vertices, 'faces': faces, 'bounds_cm': box(vertices)})
        write_new(stage/'compiled_meshes.bin', append_cache((stage/'compiled_meshes.original.bin').read_bytes(), records, material, stamp))
        write_new(stage/'assets.json', json.dumps(manifest, indent=2).encode('utf8'))
        print('Prepared four static meshes, OBJ sources, cooking inputs and appended cache; material:', material)
    elif args.mode == 'fix-winding':
        for suffix in ['Ramp','Stretch','Trim','Terrain']:
            name = 'RisenMods_Probe_'+suffix
            write_new(stage/(name+'.fixed._xmsh'), fix_winding((stage/(name+'._xmsh')).read_bytes()))
        print('Reversed only visual indices for four inverted probe meshes; all headers/vertices preserved')
    elif args.mode == 'collision-cache':
        manifest = json.loads((stage/'assets.json').read_text())
        records = [(a['name'], a['vertices'], a['faces']) for a in manifest['assets']]
        seed = (stage/'compiled_collision_meshes.original.bin').read_bytes()
        write_new(stage/'compiled_collision_meshes.bin', append_cache(seed, records, '', manifest['filetime'], collision=True))
        print('Prepared appended collision resource cache')
    else:
        manifest = json.loads((stage/'assets.json').read_text())
        for asset in manifest['assets']:
            name, bounds = asset['name'], asset['bounds_cm']
            if args.mode == 'finish':
                payload = u32(1)+(stage/(name+'.cooked')).read_bytes()+u32(0)+u32(1)+bytes(4)
                props = collision_properties(bounds)
                write_new(stage/(name+'._xcom'), resource(b'GR01CM00', 'eCCollisionMeshResource2', props, payload, manifest['filetime']))
            else:
                data = (stage/(name+'._xcom.native')).read_bytes()
                if data[:8] != b'XCOMREF1' or struct.unpack_from('<I',data,8)[0] != 1: raise ValueError('bad native triangle count')
                start, end, nv, nt = struct.unpack_from('<IIII',data,12)
                at = 28
                vertices = [list(struct.unpack_from('<fff', data, at+12*i)) for i in range(nv)]; at += 12*nv
                faces = [list(struct.unpack_from('<III',data,at+12*i)) for i in range(nt)]
                def canonical(points, indices):
                    return sorted(sorted(tuple(round(v, 3) for v in points[i]) for i in f) for f in indices)
                # Native reader emits SDK metres; compare triangles independently of welding/reordering.
                native = canonical([[v*100. for v in p] for p in vertices], faces)
                expected = canonical(asset['vertices'], asset['faces'])
                if native != expected: raise ValueError('native triangle mismatch: '+name)
                print(name, 'native authored triangles match:', nt, 'stream', start, end)


if __name__ == '__main__': main()
