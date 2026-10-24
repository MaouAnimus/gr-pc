#include "hle/gnm.h"
#include "hle/registry/adapter_registry.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <string_view>
using namespace gravity::hle::gnm;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
using Words=std::vector<std::uint32_t>;
void packet(Words& words,std::uint8_t opcode,std::initializer_list<std::uint32_t> data) {
    words.push_back(pm4::type3(opcode,static_cast<std::uint32_t>(data.size())));
    words.insert(words.end(),data);
}
pm4::Decoded decode(const Words& words,const pm4::MemoryReader& reader={},pm4::Limits limits={}) {
    const std::span<const std::uint32_t> span=words;
    return pm4::decode({&span,1},reader,limits);
}
pm4::Diagnostic submit(CommandQueue& queue,const Words& words) {
    const std::span<const std::uint32_t> span=words; return queue.submit({&span,1});
}
void decoder_test() {
    Words words{0x80000000};
    packet(words,0x10,{pm4::type3(0xFF,1),0xFFFFFFFF}); // NOP body is opaque
    packet(words,0x68,{0,11}); packet(words,0x69,{0,22,33});
    packet(words,0x76,{0,44}); packet(words,0x79,{0,55});
    packet(words,0x2F,{2}); packet(words,0x2A,{1}); packet(words,0x26,{0x1000,0});
    packet(words,0x13,{3}); packet(words,0x2D,{3,2});
    packet(words,0x27,{3,0x1000,0,3,0}); packet(words,0x35,{3,1,2,0});
    packet(words,0x12,{0}); // CLEAR_STATE
    packet(words,0x28,{0x80000000,0x80000000}); // CONTEXT_CONTROL
    packet(words,0x58,{0x2ec47fc0,0xffffffff,0,0,0,10}); // ACQUIRE_MEM
    const auto result=decode(words);
    CHECK(result.diagnostic); CHECK(result.commands.size()==11); CHECK(result.word_count==words.size());
    CHECK(std::get<pm4::RegisterWrite>(result.commands[0].operation).first==0x2000);
    CHECK(std::get<pm4::RegisterWrite>(result.commands[1].operation).values[1]==33);
    CHECK(std::get<pm4::DrawIndexed>(result.commands.back().operation).first==1);
}
void invalid_test() {
    CHECK(decode({}).diagnostic.error==pm4::Error::empty);
    CHECK(decode({0}).diagnostic.error==pm4::Error::packet_type);
    CHECK(decode({0x80000001}).diagnostic.error==pm4::Error::flags);
    CHECK(decode({pm4::type3(0x2D,2),3}).diagnostic.error==pm4::Error::truncated);
    CHECK(decode({pm4::type3(0x2D,2)|1,3,2}).diagnostic.error==pm4::Error::flags);
    CHECK(decode({pm4::type3(0x15,1),0}).diagnostic.error==pm4::Error::opcode);
    for(const auto opcode:{0x68,0x69,0x76,0x79}) {
        Words w; packet(w,opcode,{0xFFFF,1});
        CHECK(decode(w).diagnostic.error==pm4::Error::register_range);
    }
    Words w; packet(w,0x69,{0x3FF,1,2}); CHECK(!decode(w).diagnostic);
    w.clear(); packet(w,0x2A,{2}); CHECK(!decode(w).diagnostic);
    w.clear(); packet(w,0x2D,{3,0}); CHECK(!decode(w).diagnostic);
    w.clear(); packet(w,0x27,{2,0x1000,0,3,0}); CHECK(!decode(w).diagnostic);
    w.clear(); packet(w,0x35,{3,UINT32_MAX,2,0}); CHECK(!decode(w).diagnostic);
    w.clear(); packet(w,0x2F,{1,2}); CHECK(decode(w).diagnostic.error==pm4::Error::length);
    w.clear(); packet(w,0x2D,{3,2}); packet(w,0xFF,{0});
    CHECK(decode(w).commands.empty()); // atomic rejection; earlier draw is not retained
    CHECK(decode(w,{},pm4::Limits{2,10,8}).diagnostic.error==pm4::Error::budget);
    w.clear(); packet(w,0x2F,{1}); packet(w,0x2D,{3,2});
    CHECK(decode(w,{},pm4::Limits{100,1,8}).diagnostic.error==pm4::Error::budget);
}
void indirect_test() {
    Words leaf; packet(leaf,0x2D,{3,2});
    Words root; packet(root,0x3F,{0x1000,0,static_cast<std::uint32_t>(leaf.size())});
    auto reader=[&](std::uint64_t address,std::span<std::uint32_t> out) {
        if(address!=0x1000 || out.size()!=leaf.size()) return false;
        std::copy(leaf.begin(),leaf.end(),out.begin()); return true;
    };
    auto result=decode(root,reader);
    CHECK(result.diagnostic && result.commands.size()==1);
    CHECK(result.commands[0].buffer_address==0x1000);
    CHECK(decode(root).diagnostic.error==pm4::Error::memory);
    CHECK(decode(root,reader,pm4::Limits{100,10,0}).diagnostic.error==pm4::Error::recursion);
    CHECK(decode(root,reader,pm4::Limits{root.size(),10,8}).diagnostic.error==pm4::Error::budget);
    Words cycle; packet(cycle,0x3F,{0x1000,0,4});
    result=decode(cycle,[&](auto,std::span<std::uint32_t> out){std::copy(cycle.begin(),cycle.end(),out.begin());return true;});
    CHECK(result.diagnostic.error==pm4::Error::recursion);
    root.clear(); packet(root,0x3F,{0x1000,0,(1U<<20)|static_cast<std::uint32_t>(leaf.size())});
    root.push_back(0xFFFFFFFF); // chain jumps; parent tail is unreachable
    CHECK(decode(root,reader).diagnostic);
    root[3]|=1U<<24; CHECK(decode(root,reader).diagnostic.error==pm4::Error::flags);
}
void queue_test() {
    CommandQueue queue(2,6);
    Words w; packet(w,0x2D,{3,2});
    CHECK(submit(queue,w)); w[1]=999; // accepted operations own their data
    Words bad{pm4::type3(0x2D,2)};
    CHECK(!submit(queue,bad)); CHECK(queue.stats().pending==1);
    const std::span<const std::uint32_t> buffers[]={w,bad};
    const auto failure=queue.submit(buffers);
    CHECK(!failure && failure.dcb_index==1); CHECK(queue.stats().pending==1);
    CHECK(submit(queue,w)); CHECK(submit(queue,w).error==pm4::Error::busy);
    RecordingTicket ticket{123};
    CHECK(!queue.record(VK_NULL_HANDLE,{64,64},ticket)); CHECK(!ticket.id);
    CHECK(queue.stats().pending==2 && queue.stats().submitted==0);
    CHECK(!queue.acknowledge({123})); queue.reset(); CHECK(queue.stats().pending==0);
}
void concurrency_test() {
    CommandQueue queue(1000,10000);
    Words words; packet(words,0x2F,{1});
    std::atomic<int> accepted{0};
    std::vector<std::thread> threads;
    for(int i=0;i<4;++i) threads.emplace_back([&] {
        for(int j=0;j<100;++j) if(submit(queue,words)) ++accepted;
    });
    for(auto& thread:threads) thread.join();
    CHECK(accepted==400 && queue.stats().pending==400 && queue.stats().pending_words==800);
}
void adapter_test() {
    auto& registry=gravity::hle::AdapterRegistry::instance();
    registry.initialize_builtin_adapters();
    CHECK(registry.resolve("zwY0YV91TTI","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSubmitCommandBuffers));
    CHECK(sceGnmInitialize());
    CHECK(sceGnmSubmitCommandBuffers(0,nullptr,nullptr,nullptr,nullptr)!=0);
    CHECK(sceGnmSubmitCommandBuffers(1,reinterpret_cast<const std::uint32_t* const*>(1),reinterpret_cast<const std::uint32_t*>(1),nullptr,nullptr)!=0);
    Words words; packet(words,0x2F,{1});
    const std::uint32_t* address=words.data();
    std::uint32_t bytes=static_cast<std::uint32_t>(words.size()*4);
    CHECK(sceGnmSubmitCommandBuffers(1,&address,&bytes,nullptr,nullptr)==0);
    words[1]=999; CHECK(command_queue().stats().pending==1);
    const std::uint32_t constant_size=4;
    CHECK(sceGnmSubmitCommandBuffers(1,&address,&bytes,&address,&constant_size)!=0);
    bytes=3; CHECK(sceGnmSubmitCommandBuffers(1,&address,&bytes,nullptr,nullptr)!=0);
    words={pm4::type3(0xFF,1),0}; address=words.data();bytes=8;
    CHECK(sceGnmSubmitCommandBuffers(1,&address,&bytes,nullptr,nullptr)!=0);
    CHECK(command_queue().stats().pending==1);
    CHECK(registry.resolve("0H2vBYbTLHI","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDrawInitDefaultHardwareState200));
    CHECK(registry.resolve("nF6bFRUBRAU","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDispatchInitDefaultHardwareState));
    CHECK(registry.resolve("b08AgtPlHPg","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmAreSubmitsAllowed));
    CHECK(registry.resolve("f33OrruQYbM","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceRazorIsLoaded));
    CHECK(registry.resolve("iBt3Oe00Kvc","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmFlushGarlic));

    CHECK(sceGnmDrawInitDefaultHardwareState200(nullptr, 256) == 0);
    std::vector<std::uint32_t> hw_buf(256, 0);
    CHECK(sceGnmDrawInitDefaultHardwareState200(hw_buf.data(), 255) == 0);
    CHECK(sceGnmDrawInitDefaultHardwareState200(hw_buf.data(), 256) == 256);
    const auto hw_decoded = decode(hw_buf);
    CHECK(hw_decoded.diagnostic);
    CHECK(hw_decoded.word_count == 256);

    std::vector<std::uint32_t> dispatch_buf(256, 0);
    CHECK(sceGnmDispatchInitDefaultHardwareState(nullptr, 256) == 0);
    CHECK(sceGnmDispatchInitDefaultHardwareState(dispatch_buf.data(), 255) == 0);
    CHECK(sceGnmDispatchInitDefaultHardwareState(dispatch_buf.data(), 256) == 256);

    CHECK(sceGnmAreSubmitsAllowed() == 1);
    CHECK(sceRazorIsLoaded() == 0);
    sceGnmFlushGarlic();

    CHECK(registry.resolve("1qXLHIpROPE","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertWaitFlipDone));
    CHECK(registry.resolve("W1Etj-jlW7Y","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertPushMarker));
    CHECK(registry.resolve("7qZVNgEu+SY","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertPopMarker));
    CHECK(registry.resolve("jiItzS6+22g","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertSetMarker));
    CHECK(registry.resolve("aPIZJTXC+cU","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertPushColorMarker));
    CHECK(registry.resolve("aj3L-iaFmyk","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmInsertSetColorMarker));

    CHECK(sceGnmInsertWaitFlipDone(nullptr, 7, 0, 0) == -1);
    std::vector<std::uint32_t> flip_buf(7, 0);
    CHECK(sceGnmInsertWaitFlipDone(flip_buf.data(), 6, 0, 0) == -1);
    CHECK(sceGnmInsertWaitFlipDone(flip_buf.data(), 7, 1, 0) == 0);
    const auto flip_decoded = decode(flip_buf);
    CHECK(flip_decoded.diagnostic);
    CHECK(flip_decoded.word_count == 7);

    CHECK(sceGnmInsertPushMarker(nullptr, 0, "test") == -1);
    std::vector<std::uint32_t> marker_buf(16, 0);
    CHECK(sceGnmInsertPushMarker(marker_buf.data(), 16, "push") == 0);
    CHECK(sceGnmInsertPopMarker(marker_buf.data(), 16) == 0);
    CHECK(sceGnmInsertSetMarker(marker_buf.data(), 16, "set") == 0);
    CHECK(sceGnmInsertPushColorMarker(marker_buf.data(), 16, "push_color", 0xFF00FF00) == 0);
    CHECK(sceGnmInsertSetColorMarker(marker_buf.data(), 16, "set_color", 0xFFFF0000) == 0);

    CHECK(registry.resolve("bQVd5YzCal0","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetPsShader));
    CHECK(registry.resolve("4MgRw-bVNQU","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmUpdatePsShader));
    CHECK(registry.resolve("gAhCn6UiU4Y","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetVsShader));
    CHECK(registry.resolve("V31V01UiScY","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmUpdateVsShader));
    CHECK(registry.resolve("FUHG8sQ3R58","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetEsShader));
    CHECK(registry.resolve("VJNjFtqiF5w","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetHsShader));
    CHECK(registry.resolve("GNlx+y7xPdE","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmUpdateHsShader));
    CHECK(registry.resolve("vckdzbQ46SI","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetLsShader));
    CHECK(registry.resolve("KXltnCwEJHQ","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetCsShader));
    CHECK(registry.resolve("UJwNuMBcUAk","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetGsShader));
    CHECK(registry.resolve("nLM2i2+65hA","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmUpdateGsShader));
    CHECK(registry.resolve("xbxNatawohc","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSubmitAndFlipCommandBuffers));
    CHECK(registry.resolve("yvZ73uQUqrk","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSubmitDone));

    CHECK(sceGnmSetPsShader(nullptr, 40, nullptr) == -1);
    std::vector<std::uint32_t> ps_buf(40, 0);
    CHECK(sceGnmSetPsShader(ps_buf.data(), 39, nullptr) == -1);
    CHECK(sceGnmSetPsShader(ps_buf.data(), 40, nullptr) == 0);
    const auto ps_decoded = decode(ps_buf);
    CHECK(ps_decoded.diagnostic);
    CHECK(ps_decoded.word_count == 40);

    CHECK(sceGnmSetVsShader(nullptr, 29, nullptr, 0) == -1);
    std::vector<std::uint32_t> vs_buf(29, 0);
    CHECK(sceGnmSetVsShader(vs_buf.data(), 28, nullptr, 0) == -1);
    CHECK(sceGnmSetVsShader(vs_buf.data(), 29, nullptr, 0) == 0);
    const auto vs_decoded = decode(vs_buf);
    CHECK(vs_decoded.diagnostic);
    CHECK(vs_decoded.word_count == 29);

    std::vector<std::uint32_t> cs_buf(25, 0);
    CHECK(sceGnmSetCsShader(cs_buf.data(), 25, nullptr) == 0);
    CHECK(decode(cs_buf).diagnostic);

    CHECK(sceGnmSubmitDone() == 0);

    CHECK(registry.resolve("jg33rEKLfVs","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmIsUserPaEnabled));
    CHECK(registry.resolve("TLV4mswiZ4A","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDriverCaptureInProgress));
    CHECK(registry.resolve("d88anrgNoKY","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDriverTriggerCapture));
    CHECK(registry.resolve("qpGITzPE+Zc","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDebugHardwareStatus));
    CHECK(registry.resolve("iCO804ZgzdA","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmValidateCommandBuffers));
    CHECK(registry.resolve("ln33zjBrfjk","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmGetTheTessellationFactorRingBufferBaseAddress));
    CHECK(registry.resolve("26PM5Mzl8zc","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmLogicalCuIndexToPhysicalCuIndex));
    CHECK(registry.resolve("RU74kek-N0c","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmLogicalCuMaskToPhysicalCuMask));
    CHECK(registry.resolve("bX5IbRvECXk","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDingDong));
    CHECK(registry.resolve("HHo1BAljZO8","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmDisableMipStatsReport));
    CHECK(registry.resolve("f85orjx7qts","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmRequestMipStatsReportAndReset));
    CHECK(registry.resolve("+xuDhxlWRPg","libSceGnmDriver")==reinterpret_cast<std::uintptr_t>(&sceGnmSetupMipStatsReport));

    CHECK(sceGnmIsUserPaEnabled() == 0);
    CHECK(sceGnmDriverCaptureInProgress() == 0);
    sceGnmDriverTriggerCapture();
    sceGnmDebugHardwareStatus(nullptr, 0);
    CHECK(sceGnmValidateCommandBuffers(0, nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(sceGnmGetTheTessellationFactorRingBufferBaseAddress() == 0);
    CHECK(sceGnmLogicalCuIndexToPhysicalCuIndex(3) == 3);
    CHECK(sceGnmLogicalCuMaskToPhysicalCuMask(0xFF) == 0xFF);
    sceGnmDingDong(0, 0);
    sceGnmDisableMipStatsReport();
    sceGnmRequestMipStatsReportAndReset();
    sceGnmSetupMipStatsReport();

    sceGnmShutdown();
}
void vulkan_test();
int main(int argc,char** argv) {
    CHECK(argc==2); const std::string_view suite(argv[1]);
    if(suite=="decode") decoder_test();
    else if(suite=="invalid") invalid_test();
    else if(suite=="indirect") indirect_test();
    else if(suite=="queue") queue_test();
    else if(suite=="concurrency") concurrency_test();
    else if(suite=="adapter") adapter_test();
    else if(suite=="vulkan") vulkan_test();
    else return 1;
    std::puts("PM4 checks passed");
}

