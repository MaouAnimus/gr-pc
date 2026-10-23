#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::audio {

    // Forward declaration
    class AudioRenderer;

    // Get the singleton audio renderer instance.
    AudioRenderer& get_audio_renderer() noexcept;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

    // Host PCM adapter (not a binary-compatible Orbis export). Use on the opening thread.
    // Output accepts complete frames, or returns busy without consuming data.
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceAudioOutOpen(uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample,
                        uint32_t mode, uint64_t* outHandle);

    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceAudioOutRelease(uint64_t handle);

    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceAudioOutOutput(uint64_t handle, const void* buffer, uint32_t size);

    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceAudioOutGetDelay(uint64_t handle, uint64_t* delay);

    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceAudioOutGetWriteableSize(uint64_t handle, uint32_t* size);

    // NGS2 is not implemented: all calls return Error::unsupported.
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceNgs2Initialize();
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceNgs2Shutdown();
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceNgs2SetParameter(uint32_t param, uint32_t value);
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceNgs2Start();
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceNgs2Stop();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

} // namespace gravity::hle::audio