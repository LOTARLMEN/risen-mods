// Independent PhysX 2.x reference reader for xcom_check. Developer tool, never loaded by Risen.
// Minimal ABI declarations checked against SDK 2.8.1 NxStream/NxPhysicsSDK/NxTriangleMesh.
// Build x86 with MSVC; arguments: directory containing PhysXLoader.dll, corpus directory.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
using U32 = uint32_t;
class Stream {
public:
    const std::vector<uint8_t>& b; mutable size_t pos;
    Stream(const std::vector<uint8_t>& bytes, size_t at): b(bytes), pos(at) {}
    virtual ~Stream() {}
    template<class T> T read() const { T v; readBuffer(&v, sizeof v); return v; }
    virtual uint8_t readByte() const { return read<uint8_t>(); }
    virtual uint16_t readWord() const { return read<uint16_t>(); }
    virtual U32 readDword() const { return read<U32>(); }
    virtual float readFloat() const { return read<float>(); }
    virtual double readDouble() const { return read<double>(); }
    virtual void readBuffer(void* p, U32 n) const {
        if (n>b.size()-pos) throw std::runtime_error("native stream overread");
        memcpy(p,b.data()+pos,n); pos+=n;
    }
    virtual Stream& storeByte(uint8_t) { throw std::runtime_error("unexpected write"); }
    virtual Stream& storeWord(uint16_t) { throw std::runtime_error("unexpected write"); }
    virtual Stream& storeDword(U32) { throw std::runtime_error("unexpected write"); }
    virtual Stream& storeFloat(float) { throw std::runtime_error("unexpected write"); }
    virtual Stream& storeDouble(double) { throw std::runtime_error("unexpected write"); }
    virtual Stream& storeBuffer(const void*, U32) { throw std::runtime_error("unexpected write"); }
};
#define SLOT(N) virtual void unused##N() = 0;
class Mesh {
public:
    SLOT(0) SLOT(1)
    virtual U32 count(U32, U32) const = 0;
    virtual U32 format(U32, U32) const = 0;
    virtual const void* base(U32, U32) const = 0;
    virtual U32 stride(U32, U32) const = 0;
};
class SDK {
public:
    SLOT(0) SLOT(1) SLOT(2) SLOT(3) SLOT(4) SLOT(5) SLOT(6) SLOT(7)
    virtual Mesh* triangle(const Stream&) = 0;
    virtual void releaseTriangle(Mesh&) = 0;
    SLOT(10) SLOT(11) SLOT(12) SLOT(13) SLOT(14) SLOT(15) SLOT(16) SLOT(17)
    virtual Mesh* convex(const Stream&) = 0;
    virtual void releaseConvex(Mesh&) = 0;
};
class Errors {
public:
    virtual void error(int n,const char* message,const char*,int) { fprintf(stderr,"PhysX %d: %s\n",n,message); }
    virtual int assertion(const char* message,const char*,int) { fprintf(stderr,"PhysX assertion: %s\n",message); return 2; }
    virtual void print(const char* message) { fprintf(stderr,"%s\n",message); }
protected: virtual ~Errors() {}
};
class OutputErrors: public Errors { public: ~OutputErrors() {} };
static void put(std::ofstream& out,U32 n) { out.write(reinterpret_cast<const char*>(&n),4); }
int main(int argc,char**argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: xcom-native-check <PhysX DLL directory> <corpus directory>");
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
        SetDllDirectoryA(argv[1]);
        auto path=std::filesystem::path(argv[1])/"PhysXLoader.dll";
        HMODULE lib=LoadLibraryW(path.c_str());
        if(!lib) throw std::runtime_error("cannot load PhysXLoader.dll");
        using Create=SDK*(__cdecl*)(U32,void*,Errors*,const void*,void*);
        using Release=void(__cdecl*)(SDK*);
        auto create=reinterpret_cast<Create>(GetProcAddress(lib,"NxCreatePhysicsSDK"));
        auto release=reinterpret_cast<Release>(GetProcAddress(lib,"NxReleasePhysicsSDK"));
        if(!create||!release) throw std::runtime_error("missing SDK exports");
        int desc[7]={65536,256,2048,0,1,128,-1}; OutputErrors errors;
        int sdkError=0;
        SDK* sdk=create(0x02080100,nullptr,&errors,desc,&sdkError);
        if(!sdk) {
            fprintf(stderr,"loader create error %d; trying bundled core directly\n",sdkError);
            path=std::filesystem::path(argv[1])/"PhysXCore.dll";
            HMODULE core=LoadLibraryW(path.c_str());
            if(!core) throw std::runtime_error("cannot load PhysXCore.dll");
            create=reinterpret_cast<Create>(GetProcAddress(core,"NpCreatePhysicsSDK"));
            release=reinterpret_cast<Release>(GetProcAddress(core,"NpReleasePhysicsSDK"));
            if(!create||!release) throw std::runtime_error("missing core SDK exports");
            for(U32 version: {0x02080100u,0x02080400u}) {
                sdk=create(version,nullptr,&errors,desc,&sdkError);
                if(sdk) { fprintf(stderr,"using core API 0x%08X\n",version);break; }
                fprintf(stderr,"core create error %d for API 0x%08X\n",sdkError,version);
            }
        }
        if(!sdk) throw std::runtime_error("cannot create PhysX SDK 2.8.1");
        size_t files=0,meshes=0;
        for(auto& entry: std::filesystem::directory_iterator(argv[2])) {
            if(entry.path().extension()!=L"._xcom") continue;
            std::ifstream f(entry.path(),std::ios::binary);
            std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
            if(b.size()<24||memcmp(b.data(),"GR01CM00",8)) throw std::runtime_error("bad xcom header");
            U32 at; memcpy(&at,b.data()+16,4);
            if(at>b.size()) throw std::runtime_error("bad payload offset");
            Stream stream(b,at);
            auto dest=entry.path(); dest+=L".native";
            std::ofstream out(dest,std::ios::binary|std::ios::trunc);
            out.write("XCOMREF1",8);
            for(U32 kind=0;kind<2;kind++) {
                U32 n=stream.readDword(); put(out,n);
                if(n>100000) throw std::runtime_error("bad mesh count");
                for(U32 i=0;i<n;i++) {
                    U32 start=static_cast<U32>(stream.pos);
                    Mesh* mesh=kind?sdk->convex(stream):sdk->triangle(stream);
                    if(!mesh) { fprintf(stderr,"failed %s [%u:%u] at %u\n",entry.path().string().c_str(),kind,i,start); throw std::runtime_error("native mesh refused"); }
                    put(out,start); put(out,static_cast<U32>(stream.pos));
                    U32 nv=mesh->count(0,1),nt=mesh->count(0,0);
                    put(out,nv);put(out,nt);
                    auto v=static_cast<const char*>(mesh->base(0,1));
                    for(U32 j=0;j<nv;j++) out.write(v+j*mesh->stride(0,1),12);
                    auto t=static_cast<const char*>(mesh->base(0,0));
                    U32 width=mesh->format(0,0)==2?1:mesh->format(0,0)==3?2:4;
                    for(U32 j=0;j<nt;j++) for(U32 k=0;k<3;k++) {
                        U32 index=0;memcpy(&index,t+j*mesh->stride(0,0)+k*width,width);put(out,index);
                    }
                    if(kind) sdk->releaseConvex(*mesh); else sdk->releaseTriangle(*mesh);
                    meshes++;
                }
            }
            put(out,static_cast<U32>(stream.pos));
            if(!out) throw std::runtime_error("reference write failed");
            files++;
        }
        release(sdk);
        printf("%zu files, %zu meshes read by native PhysX; reference buffers written\n",files,meshes);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what());return 1; }
}
