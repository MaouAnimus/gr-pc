#pragma once

#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::avplayer {

typedef void* SceAvPlayerHandle;

struct SceAvPlayerInitData {
    std::uint32_t memoryReplacement;
    std::uint32_t reserved1;
    void* allocate;
    void* deallocate;
    void* allocateTexture;
    void* deallocateTexture;
    std::uint32_t defaultStreamNum;
    std::uint32_t reserved2;
};

struct SceAvPlayerStreamInfo {
    std::uint32_t type; // 0 = video, 1 = audio, 2 = timed text
    std::uint32_t details;
    std::uint32_t width;
    std::uint32_t height;
    float framerate;
    std::uint32_t channelCount;
    std::uint32_t sampleRate;
    std::uint32_t languageCode;
};

struct SceAvPlayerFrameInfoEx {
    void* pData;
    std::uint64_t timestamp;
    std::uint32_t details;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
    std::uint32_t pixelFormat;
    std::uint32_t aspectRatio;
};

struct SceAvPlayerAudioInfo {
    void* pData;
    std::uint64_t timestamp;
    std::uint32_t size;
    std::uint32_t channelCount;
    std::uint32_t sampleRate;
};

extern "C" {

GRAVITY_SYSV_ABI SceAvPlayerHandle sceAvPlayerInit(const SceAvPlayerInitData* initData);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPostInit(SceAvPlayerHandle player, const void* reserved);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerClose(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerAddSource(SceAvPlayerHandle player, const char* filename);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerSetLooping(SceAvPlayerHandle player, std::uint32_t loop);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStart(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStop(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPause(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerResume(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerIsActive(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::uint32_t sceAvPlayerStreamCount(SceAvPlayerHandle player);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetStreamInfo(SceAvPlayerHandle player, std::uint32_t streamIndex, SceAvPlayerStreamInfo* info);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerEnableStream(SceAvPlayerHandle player, std::uint32_t streamIndex);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetVideoDataEx(SceAvPlayerHandle player, SceAvPlayerFrameInfoEx* frameInfo);
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetAudioData(SceAvPlayerHandle player, SceAvPlayerAudioInfo* audioInfo);

} // extern "C"

} // namespace gravity::hle::avplayer
