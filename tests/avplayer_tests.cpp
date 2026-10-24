#include "hle/avplayer/avplayer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::avplayer;

static void test_lifecycle() {
    SceAvPlayerInitData initData{};
    SceAvPlayerHandle handle = sceAvPlayerInit(&initData);
    CHECK(handle != nullptr);
    CHECK(sceAvPlayerPostInit(handle, nullptr) == 0);
    CHECK(sceAvPlayerIsActive(handle) == 0);
    CHECK(sceAvPlayerClose(handle) == 0);
    CHECK(sceAvPlayerClose(handle) != 0); // Already closed
    std::printf("[PASS] avplayer_lifecycle\n");
}

static void test_streams_and_playback() {
    SceAvPlayerInitData initData{};
    SceAvPlayerHandle handle = sceAvPlayerInit(&initData);
    CHECK(handle != nullptr);

    CHECK(sceAvPlayerAddSource(handle, "/app0/movie/logo.mp4") == 0);
    CHECK(sceAvPlayerSetLooping(handle, 1) == 0);

    const auto stream_cnt = sceAvPlayerStreamCount(handle);
    CHECK(stream_cnt == 2);

    SceAvPlayerStreamInfo vinfo{};
    CHECK(sceAvPlayerGetStreamInfo(handle, 0, &vinfo) == 0);
    CHECK(vinfo.type == 0); // video
    CHECK(vinfo.width == 1920);
    CHECK(vinfo.height == 1080);

    SceAvPlayerStreamInfo ainfo{};
    CHECK(sceAvPlayerGetStreamInfo(handle, 1, &ainfo) == 0);
    CHECK(ainfo.type == 1); // audio
    CHECK(ainfo.sampleRate == 48000);
    CHECK(ainfo.channelCount == 2);

    CHECK(sceAvPlayerEnableStream(handle, 0) == 0);
    CHECK(sceAvPlayerEnableStream(handle, 1) == 0);

    CHECK(sceAvPlayerStart(handle) == 0);
    CHECK(sceAvPlayerIsActive(handle) == 1);

    SceAvPlayerFrameInfoEx f_ex{};
    CHECK(sceAvPlayerGetVideoDataEx(handle, &f_ex) == 0);
    CHECK(f_ex.pData != nullptr);
    CHECK(f_ex.width == 1920);
    CHECK(f_ex.height == 1080);

    SceAvPlayerAudioInfo a_inf{};
    CHECK(sceAvPlayerGetAudioData(handle, &a_inf) == 0);
    CHECK(a_inf.pData != nullptr);
    CHECK(a_inf.size > 0);

    CHECK(sceAvPlayerPause(handle) == 0);
    CHECK(sceAvPlayerIsActive(handle) == 0);
    CHECK(sceAvPlayerResume(handle) == 0);
    CHECK(sceAvPlayerIsActive(handle) == 1);

    CHECK(sceAvPlayerStop(handle) == 0);
    CHECK(sceAvPlayerIsActive(handle) == 0);

    CHECK(sceAvPlayerClose(handle) == 0);
    std::printf("[PASS] avplayer_streams_and_playback\n");
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view suite(argv[1]);
        if (suite == "lifecycle") { test_lifecycle(); return 0; }
        if (suite == "playback") { test_streams_and_playback(); return 0; }
    }
    test_lifecycle();
    test_streams_and_playback();
    return 0;
}
