#include "hle/gnm.h"
#include <Windows.h>
#include <cstdio>
#include <exception>

namespace gravity::hle::gnm {
namespace {
bool snapshot(std::uint64_t address,void* out,std::size_t bytes) {
    if (!address || !out || !bytes || address>UINTPTR_MAX-bytes) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,bytes,&copied) && copied==bytes;
}
std::int32_t rejected(const char* message,std::uint32_t code=0x80D11000U) {
    std::fprintf(stderr,"sceGnmSubmitCommandBuffers: %s\n",message);
    return static_cast<std::int32_t>(code);
}
}
extern "C" GRAVITY_SYSV_ABI int32_t sceGnmSubmitCommandBuffers(
    uint32_t count,const uint32_t* const* dcb_addresses,const uint32_t* dcb_sizes,
    const uint32_t* const* ccb_addresses,const uint32_t* ccb_sizes) try {
    if (!gnm_is_initialized()) return rejected("GNM is not initialized");
    if (!count || count>64 || !dcb_addresses || !dcb_sizes || (!!ccb_addresses!=!!ccb_sizes))
        return rejected("Invalid command-buffer array/count");
    std::vector<const std::uint32_t*> addresses(count);
    std::vector<std::uint32_t> sizes(count);
    if (!snapshot(reinterpret_cast<std::uintptr_t>(dcb_addresses),addresses.data(),count*sizeof(addresses[0])) ||
        !snapshot(reinterpret_cast<std::uintptr_t>(dcb_sizes),sizes.data(),count*sizeof(sizes[0])))
        return rejected("Cannot read command-buffer metadata");
    if (ccb_sizes) {
        std::vector<std::uint32_t> constant_sizes(count);
        if (!snapshot(reinterpret_cast<std::uintptr_t>(ccb_sizes),constant_sizes.data(),count*sizeof(constant_sizes[0])))
            return rejected("Cannot read constant-buffer metadata");
        for(auto size:constant_sizes) if(size) return rejected("Constant-engine buffers are not implemented");
    }
    const pm4::Limits limits;
    std::size_t total=0;
    for(std::size_t i=0;i<count;++i) {
        const auto address=reinterpret_cast<std::uintptr_t>(addresses[i]);
        if (!sizes[i] || sizes[i]%4 || !address || address%4 || sizes[i]/4>limits.max_words-total)
            return rejected("Invalid DCB size/alignment/budget");
        total+=sizes[i]/4;
    }
    std::vector<std::vector<std::uint32_t>> snapshots(count);
    std::vector<std::span<const std::uint32_t>> buffers;
    buffers.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        snapshots[i].resize(sizes[i]/4);
        if (!snapshot(reinterpret_cast<std::uintptr_t>(addresses[i]),snapshots[i].data(),sizes[i]))
            return rejected("Cannot snapshot DCB memory");
        buffers.emplace_back(snapshots[i]);
    }
    const auto result=command_queue().submit(buffers,[](std::uint64_t address,std::span<std::uint32_t> out) {
        return snapshot(address,out.data(),out.size_bytes());
    });
    if (!result) {
        std::fprintf(stderr,"PM4 rejected: DCB=%zu address=0x%llx DWORD=%zu opcode=0x%x: %s\n",
            result.dcb_index,static_cast<unsigned long long>(result.buffer_address),result.word_offset,result.opcode,result.message);
        return static_cast<std::int32_t>(result.error==pm4::Error::busy?0x80D11001U:0x80D11000U);
    }
    return 0; // Accepted by the CPU queue, not GPU completion.
} catch (const std::exception& error) {
    return rejected(error.what(),0x80D110FFU);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceGnmSubmitAndFlipCommandBuffers(
    uint32_t count, const uint32_t* const* dcb_addresses, const uint32_t* dcb_sizes,
    const uint32_t* const* ccb_addresses, const uint32_t* ccb_sizes,
    uint32_t video_out_handle, uint32_t display_buffer_index, uint32_t flip_mode, int64_t flip_arg) {
    (void)video_out_handle;
    (void)display_buffer_index;
    (void)flip_mode;
    (void)flip_arg;
    return sceGnmSubmitCommandBuffers(count, dcb_addresses, dcb_sizes, ccb_addresses, ccb_sizes);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceGnmSubmitDone() {
    return 0;
}

} // namespace gravity::hle::gnm

