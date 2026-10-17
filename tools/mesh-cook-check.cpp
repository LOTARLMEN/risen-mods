// Offline PhysX 2.x triangle cooker probe, x86 only. Never loaded by the game.
// Input: MESHCHK1, uint32 vertex/triangle counts, float3 vertices (metres), uint32 triplets.
// Output: native cooked stream. The existing independent xcom reader validates the result.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>
#include <stdexcept>
using U32 = uint32_t;
class Output {
public:
    std::vector<uint8_t> bytes;
    virtual ~Output() {}
    virtual uint8_t readByte() const { throw std::runtime_error("unexpected read"); }
    virtual uint16_t readWord() const { throw std::runtime_error("unexpected read"); }
    virtual U32 readDword() const { throw std::runtime_error("unexpected read"); }
    virtual float readFloat() const { throw std::runtime_error("unexpected read"); }
    virtual double readDouble() const { throw std::runtime_error("unexpected read"); }
    virtual void readBuffer(void*, U32) const { throw std::runtime_error("unexpected read"); }
    virtual Output& storeByte(uint8_t v) { return storeBuffer(&v, sizeof v); }
    virtual Output& storeWord(uint16_t v) { return storeBuffer(&v, sizeof v); }
    virtual Output& storeDword(U32 v) { return storeBuffer(&v, sizeof v); }
    virtual Output& storeFloat(float v) { return storeBuffer(&v, sizeof v); }
    virtual Output& storeDouble(double v) { return storeBuffer(&v, sizeof v); }
    virtual Output& storeBuffer(const void* p, U32 n) {
        auto b = static_cast<const uint8_t*>(p); bytes.insert(bytes.end(), b, b+n); return *this;
    }
};
// NxTriangleMeshDesc ABI, verified against the SDK 2.x header and installed cooking DLL.
struct Desc {
    U32 vertices, triangles, pointStride, triangleStride;
    const void* points; const void* indices;
    U32 flags, materialStride; const void* materials;
    U32 heightAxis; float heightExtent; void* pmap; float convexEdgeThreshold;
};
static_assert(sizeof(Desc)==52, "requires x86 ABI");
int main(int argc,char**argv) {
    try {
        if(argc!=4) throw std::runtime_error("usage: mesh-cook-check <PhysXCooking.dll> <input> <output>");
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
        HMODULE lib=LoadLibraryA(argv[1]);
        if(!lib) throw std::runtime_error("cannot load cooking DLL");
        auto init=reinterpret_cast<bool(__cdecl*)(void*,void*)>(GetProcAddress(lib,"NxInitCooking"));
        auto cook=reinterpret_cast<bool(__cdecl*)(const Desc&,Output&)>(GetProcAddress(lib,"NxCookTriangleMesh"));
        auto close=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(lib,"NxCloseCooking"));
        if(!init||!cook||!close) throw std::runtime_error("missing cooking exports");
        std::ifstream in(argv[2],std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});
        if(data.size()<16 || memcmp(data.data(),"MESHCHK1",8)) throw std::runtime_error("invalid input");
        U32 nv,nt; memcpy(&nv,data.data()+8,4); memcpy(&nt,data.data()+12,4);
        if(!nv||!nt||nv>1000000||nt>1000000||data.size()!=16ull+12ull*(nv+nt)) throw std::runtime_error("invalid counts");
        for(size_t i=16;i<16ull+12ull*nv;i+=4) {
            float v;memcpy(&v,data.data()+i,4);if(!std::isfinite(v)) throw std::runtime_error("nonfinite vertex");
        }
        for(size_t i=16ull+12ull*nv;i<data.size();i+=4) {
            U32 index;memcpy(&index,data.data()+i,4);if(index>=nv) throw std::runtime_error("invalid index");
        }
        if(!init(nullptr,nullptr)) throw std::runtime_error("cooking init refused");
        Desc desc{};desc.vertices=nv;desc.triangles=nt;desc.pointStride=desc.triangleStride=12;
        desc.points=data.data()+16;desc.indices=data.data()+16+12*nv;
        desc.heightAxis=0xff;desc.convexEdgeThreshold=0.001f;
        Output out;bool ok=cook(desc,out);close();
        if(!ok||out.bytes.empty()) throw std::runtime_error("cooking refused");
        std::ofstream file(argv[3],std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(out.bytes.data()),out.bytes.size());
        if(!file) throw std::runtime_error("output write failed");
        printf("Cooked %u vertices/%u triangles into %zu bytes\n",nv,nt,out.bytes.size());
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
