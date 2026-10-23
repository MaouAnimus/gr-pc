#include "hle/loader/elf.h"
#include "hle/loader/reloc.h"
#include "hle/loader/abi_thunk.h"
#include "hle/fs.h"
#include "hle/video.h"
#include "hle/audio.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>
using namespace gravity;
using namespace gravity::hle::loader;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
template<class T> void put(std::vector<std::uint8_t>& b, std::size_t off, T v) { std::memcpy(b.data()+off, &v, sizeof(v)); }
std::vector<std::uint8_t> elf(bool dynamic = false) {
    std::vector<std::uint8_t> b(0x2400);
    std::memcpy(b.data(), "\x7f" "ELF\x02\x01\x01", 7);
    put<std::uint16_t>(b,16,3); put<std::uint16_t>(b,18,62); put<std::uint32_t>(b,20,1);
    put<std::uint64_t>(b,24,0); put<std::uint64_t>(b,32,64);
    put<std::uint16_t>(b,52,64); put<std::uint16_t>(b,54,56); put<std::uint16_t>(b,56,dynamic?4:2);
    auto ph = [&](int index, std::uint32_t type, std::uint32_t flags, std::uint64_t off,
                  std::uint64_t addr, std::uint64_t filesz, std::uint64_t memsz) {
        auto p=64+index*56;
        put(b,p,type); put(b,p+4,flags); put(b,p+8,off); put(b,p+16,addr);
        put(b,p+32,filesz); put(b,p+40,memsz); put<std::uint64_t>(b,p+48,1);
    };
    ph(0,1,5,0x1000,0,16,4096); ph(1,1,6,0x1800,0x2000,16,4096);
    b[0x1000]=0xC3; b[0x1800]=0x42;
    if (dynamic) {
        ph(2,0x61000000,4,0x2000,0,0x400,0);
        ph(3,2,4,0x2100,0,160,160);
        const std::uint64_t tags[][2]={{0x61000035,0},{0x61000037,16},{0x61000039,16},
            {0x6100003F,48},{0x6100003B,24},{0x6100002F,64},{0x61000031,24},
            {0x61000033,24},{0x6100002B,7},{0,0}};
        std::memcpy(b.data()+0x2100,tags,sizeof(tags));
        put<std::uint64_t>(b,0x2040,0x2000);
        put<std::uint64_t>(b,0x2048,99); // unsupported relocation triggers rollback
    }
    return b;
}
void loader() {
    auto good=elf();
    memory::AddressSpace as;
    auto rejected=[&](const auto& bytes) {
        auto r=load_self(bytes.data(),bytes.size(),as);
        CHECK(!r.base && r.error); CHECK(!as.base());
    };
    auto b=good; b.resize(63); rejected(b);
    b=good; put<std::uint16_t>(b,54,65535); rejected(b);
    b=good; put<std::uint64_t>(b,32,UINT64_MAX); rejected(b);
    b=good; put<std::uint64_t>(b,64+32,4097); rejected(b);
    b=good; put<std::uint64_t>(b,64+8,UINT64_MAX); rejected(b);
    b=good; put<std::uint64_t>(b,24,0x2000); rejected(b);
    b=good; put<std::uint64_t>(b,64+56+16,0); rejected(b);
    b=good; put<std::uint32_t>(b,64+4,7); rejected(b);
    rejected(elf(true)); // includes committed pages; ownership rollback releases reservation
    CHECK(as.reserve());
    CHECK(as.commit(0x2000,4096));
    auto* sentinel=static_cast<std::uint8_t*>(as.base())+0x2000; *sentinel=0xA5;
    CHECK(!load_self(good.data(),good.size(),as).base); CHECK(*sentinel==0xA5);
    MEMORY_BASIC_INFORMATION info{}; CHECK(VirtualQuery(as.base(),&info,sizeof(info))); CHECK(info.State==MEM_RESERVE);
    CHECK(as.decommit(0x2000,4096));
    auto bad=elf(true); CHECK(!load_self(bad.data(),bad.size(),as).base);
    CHECK(VirtualQuery(as.base(),&info,sizeof(info))); CHECK(info.State==MEM_RESERVE);
    auto r=load_self(good.data(),good.size(),as); CHECK(r.base && !r.error);
    CHECK(*reinterpret_cast<std::uint8_t*>(r.entry)==0xC3);
    CHECK(static_cast<std::uint8_t*>(as.base())[0x2010]==0); // BSS
    CHECK(VirtualQuery(as.base(),&info,sizeof(info))); CHECK(info.Protect==PAGE_EXECUTE_READ);
    CHECK(as.release());
    // A valid image remains loadable after all failures.
    CHECK(load_self(good.data(),good.size(),as).base); CHECK(as.release());
}
void relocations() {
    memory::AddressSpace as; CHECK(as.reserve()); CHECK(as.commit(0,4096));
    const auto base=reinterpret_cast<std::uintptr_t>(as.base());
    std::vector<std::uint8_t> b(256);
    std::memcpy(b.data(),"unknown",8);
    RelocationLayout layout{0,16,16,48,64,48,112,24};
    const MemoryRange ranges[]={{0,4096}};
    RelocationReport report;
    auto apply=[&](bool inspect=false) { return apply_orbis_relocations(b.data(),b.size(),base,4096,layout,ranges,report,inspect); };
    put<std::uint64_t>(b,64,0); put<std::uint64_t>(b,72,8); put<std::uint64_t>(b,80,16);
    put<std::uint64_t>(b,88,8); put<std::uint64_t>(b,96,99);
    auto* data=static_cast<std::uint64_t*>(as.base()); *data=0x1234;
    CHECK(apply()<0); CHECK(*data==0x1234); // no partial writes
    put<std::uint64_t>(b,96,(1ULL<<32)|7); // undefined symbol index 1
    CHECK(apply()<0); CHECK(report.unresolved.size()==1); CHECK(*data==0x1234);
    CHECK(apply(true)==2); CHECK(data[0]==base+16); CHECK(data[1]==0);
    put<std::uint64_t>(b,112,16); put<std::uint64_t>(b,120,8); put<std::uint64_t>(b,128,32);
    CHECK(apply(true)==3); CHECK(data[2]==base+32); // separate PLT table
    put<std::uint64_t>(b,64,UINT64_MAX); CHECK(apply(true)<0);
    put<std::uint64_t>(b,64,4095); CHECK(apply(true)<0);
    put<std::uint64_t>(b,64,0); put<std::uint64_t>(b,96,(999ULL<<32)|7); CHECK(apply(true)<0);
    put<std::uint64_t>(b,96,(1ULL<<32)|7);
    std::memset(b.data(), 'x',16); CHECK(apply(true)<0); // bounded symbol string
    std::memcpy(b.data(),"unknown",8); layout.rela_size=47; CHECK(apply(true)<0);
    layout.rela_size=48; layout.symbols=SIZE_MAX; CHECK(apply(true)<0);
    CHECK(!apply_relocations(b.data(),b.size(),base));
}
void fs() {
    using namespace hle::fs;
    shutdown_vfs();
    CHECK(sceFios2Open("/app0/does-not-exist",0,0)==-1); // must not deadlock
    shutdown_vfs();
    const auto root=std::filesystem::temp_directory_path()/("gravity-vfs-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root/"app");
    struct Cleanup { std::filesystem::path root; ~Cleanup(){ shutdown_vfs(); std::filesystem::remove_all(root); } } cleanup{root};
    { std::ofstream(root/"app"/"inside.txt")<<"hello"; std::ofstream(root/"outside.txt")<<"secret"; }
    CHECK(initialize_vfs((root/"app").wstring()));
    CHECK(sceFios2Open("/app0/../outside.txt",0,0)==-1);
    CHECK(sceFios2Open("/app0/..\\outside.txt",0,0)==-1);
    CHECK(sceFios2Open("/app0/C:/Windows/win.ini",0,0)==-1);
    CHECK(sceFios2Open("/app0/inside.txt",2,0)==-1);
    auto fd=sceFios2Open("/app0/inside.txt",0,0); CHECK(fd>=0);
    char out[6]{}; CHECK(sceFios2Read(fd,out,5)==5); CHECK(std::string_view(out)=="hello");
    CHECK(sceFios2GetStat(fd,out)==-1); CHECK(sceFios2Close(fd)==0);
}
void headless() {
    using namespace hle::video;
    CHECK(initialize_headless()); CHECK(video_is_initialized()); CHECK(video_is_headless());
    CHECK(sceVideoOutSetMode(0,1080,60).error==hle::Error::invalid_argument);
    CHECK(sceVideoOutSetMode(1280,720,60)); sceVideoOutWaitVblank();
    CHECK(video_presented_frames()==0);
    sceVideoOutShutdown(); CHECK(!video_is_initialized());
    CHECK(hle::audio::sceNgs2Initialize().error==hle::Error::unsupported);
    std::uint64_t handle=123;
    CHECK(!hle::audio::sceAudioOutOpen(0,2,16,0,&handle)); CHECK(handle==0);
    CHECK(!hle::audio::sceAudioOutOpen(48000,2,16,0,nullptr));
    CHECK(!hle::audio::sceAudioOutRelease(123));
}
int main(int argc,char**argv) {
    CHECK(argc==2); const std::string_view suite(argv[1]);
    if(suite=="loader") loader();
    else if(suite=="relocations") relocations();
    else if(suite=="fs") fs();
    else if(suite=="headless") headless();
    else if(suite=="abi") CHECK(sysv_to_win64_thunk(1,2,4,8,16,32)==63);
    else return 1;
    std::puts("regression checks passed");
}
