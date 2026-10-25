#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::playgo {

struct ScePlayGoInitParams {
    void* buffer{nullptr};
    std::uint32_t buffer_size{0};
    std::uint32_t reserved{0};
};

constexpr std::int32_t SCE_PLAYGO_OK                     = 0;
constexpr std::int32_t SCE_PLAYGO_ERROR_INVALID_ARGUMENT = static_cast<std::int32_t>(0x80AF0001);

enum ScePlayGoLocus : std::uint8_t {
    SCE_PLAYGO_LOCUS_NONE  = 0,
    SCE_PLAYGO_LOCUS_BD    = 1,
    SCE_PLAYGO_LOCUS_NET   = 2,
    SCE_PLAYGO_LOCUS_LOCAL = 3,
};

extern "C" {
GRAVITY_SYSV_ABI std::int32_t scePlayGoInitialize(const ScePlayGoInitParams* params);
GRAVITY_SYSV_ABI std::int32_t scePlayGoTerminate();
GRAVITY_SYSV_ABI std::int32_t scePlayGoOpen(std::int32_t* out_handle, const void* reserved);
GRAVITY_SYSV_ABI std::int32_t scePlayGoClose(std::int32_t handle);
GRAVITY_SYSV_ABI std::int32_t scePlayGoGetToDoList(
    std::int32_t handle, void* out_chunks, std::uint32_t max_count, std::uint32_t* out_count);
GRAVITY_SYSV_ABI std::int32_t scePlayGoGetInstallSpeed(std::int32_t handle, std::uint32_t* out_speed);
GRAVITY_SYSV_ABI std::int32_t scePlayGoSetInstallSpeed(std::int32_t handle, std::uint32_t speed);
GRAVITY_SYSV_ABI std::int32_t scePlayGoGetLocus(
    std::int32_t handle, const std::uint16_t* chunk_ids, std::uint32_t count, std::uint8_t* out_locus);
GRAVITY_SYSV_ABI std::int32_t scePlayGoGetEta(std::int32_t handle, std::uint32_t* out_eta);
}

[[nodiscard]] bool is_initialized() noexcept;
void reset() noexcept;

} // namespace gravity::hle::playgo
