#include "hle/audio/ngs2.h"
#include "hle/audio/audio3d.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::audio;

static void test_ngs2() {
    std::uint32_t sys_h = 0;
    CHECK(sceNgs2SystemCreateWithAllocator(nullptr, nullptr, &sys_h) == 0);
    CHECK(sys_h != 0);

    std::size_t buf_size = 0;
    CHECK(sceNgs2RackQueryBufferSize(nullptr, &buf_size) == 0);
    CHECK(buf_size >= 65536);

    std::uint32_t rack_h = 0;
    CHECK(sceNgs2RackCreateWithAllocator(sys_h, nullptr, nullptr, &rack_h) == 0);
    CHECK(rack_h != 0);

    std::uint32_t voice_h = 0;
    CHECK(sceNgs2RackGetVoiceHandle(rack_h, 0, &voice_h) == 0);
    CHECK(voice_h != 0);

    std::uint32_t state = 99;
    CHECK(sceNgs2VoiceGetState(voice_h, &state) == 0);
    CHECK(state == 0); // idle

    CHECK(sceNgs2VoiceControl(voice_h, 0, nullptr) == 0); // start
    CHECK(sceNgs2VoiceGetState(voice_h, &state) == 0);
    CHECK(state == 1); // playing

    std::vector<std::int16_t> pcm_buf(1024 * 2, 42);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 1024) == 0);
    // Buffer should be cleared / written with silence
    CHECK(pcm_buf[0] == 0);

    CHECK(sceNgs2RackDestroy(rack_h) == 0);
    CHECK(sceNgs2SystemDestroy(sys_h) == 0);
    std::printf("[PASS] ngs2_system_and_racks\n");
}

static void test_audio3d() {
    CHECK(sceAudio3dInitialize(nullptr) == 0);

    std::uint32_t port_h = 0;
    CHECK(sceAudio3dPortOpen(nullptr, &port_h) == 0);
    CHECK(port_h != 0);

    std::uint32_t obj_h = 0;
    CHECK(sceAudio3dObjectReserve(port_h, &obj_h) == 0);
    CHECK(obj_h != 0);

    float coords[3] = { 10.0f, 20.0f, -5.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, coords) == 0);

    CHECK(sceAudio3dPortFlush(port_h) == 0);
    CHECK(sceAudio3dObjectUnreserve(port_h, obj_h) == 0);
    CHECK(sceAudio3dPortClose(port_h) == 0);
    CHECK(sceAudio3dTerminate() == 0);
    std::printf("[PASS] audio3d_lifecycle\n");
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view suite(argv[1]);
        if (suite == "ngs2") { test_ngs2(); return 0; }
        if (suite == "audio3d") { test_audio3d(); return 0; }
    }
    test_ngs2();
    test_audio3d();
    return 0;
}
