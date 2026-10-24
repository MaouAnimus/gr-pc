#include "hle/audio/ngs2.h"
#include "hle/audio/audio3d.h"
#include <algorithm>
#include <cmath>
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

    // 1. Start voice (Command 0)
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_PLAY, nullptr) == 0);
    CHECK(sceNgs2VoiceGetState(voice_h, &state) == 0);
    CHECK(state == 1); // playing

    // 2. Render audio: verify phase starts at 0, but subsequent samples are synthesized
    std::vector<std::int16_t> pcm_buf(1024 * 2, 42);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 1024) == 0);
    CHECK(pcm_buf[0] == 0); // phase == 0.0 at t=0
    bool has_nonzero = false;
    for (std::size_t i = 1; i < 2048; ++i) {
        if (pcm_buf[i] != 0) {
            has_nonzero = true;
            break;
        }
    }
    CHECK(has_nonzero);

    // 3. Pause voice (Command 1)
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_PAUSE, nullptr) == 0);
    CHECK(sceNgs2VoiceGetState(voice_h, &state) == 0);
    CHECK(state == 2); // paused

    // 4. Stop voice (Command 2)
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_STOP, nullptr) == 0);
    CHECK(sceNgs2VoiceGetState(voice_h, &state) == 0);
    CHECK(state == 0); // idle

    // Render when stopped should yield silence
    std::fill(pcm_buf.begin(), pcm_buf.end(), 1234);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 1024) == 0);
    for (auto s : pcm_buf) {
        CHECK(s == 0);
    }

    // 5. Volume modulation (Command 3)
    float vol = 0.5f;
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_VOLUME, &vol) == 0);
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_PLAY, nullptr) == 0);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 1024) == 0);
    CHECK(pcm_buf[0] == 0);

    // 6. Pitch modulation (Command 4)
    float pitch = 2.0f;
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_PITCH, &pitch) == 0);

    // 7. Pan modulation: Hard Left (Command 5)
    float pan_left = -1.0f;
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_PAN, &pan_left) == 0);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 512) == 0);
    for (std::size_t i = 0; i < 512; ++i) {
        CHECK(pcm_buf[i * 2 + 1] == 0); // Right channel must be silent
    }

    // Pan modulation: Hard Right
    float pan_right = 1.0f;
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_PAN, &pan_right) == 0);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 512) == 0);
    for (std::size_t i = 0; i < 512; ++i) {
        CHECK(pcm_buf[i * 2 + 0] == 0); // Left channel must be silent
    }

    // 8. PCM Buffer Playback (Command 6)
    std::vector<std::int16_t> custom_pcm = { 5000, 10000, 15000, 20000, 25000 };
    SceNgs2VoiceBufferParams bp{};
    bp.data = custom_pcm.data();
    bp.size_bytes = static_cast<std::uint32_t>(custom_pcm.size() * sizeof(std::int16_t));
    bp.loop = 1;
    float pan_center = 0.0f;
    vol = 1.0f;
    pitch = 1.0f;
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_PAN, &pan_center) == 0);
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_VOLUME, &vol) == 0);
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_PITCH, &pitch) == 0);
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_BUFFER, &bp) == 0);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 5) == 0);
    // Verified buffer samples played
    CHECK(pcm_buf[0] == 5000);

    // 9. Multi-voice mixing and clamping without overflow
    for (std::uint32_t v_idx = 0; v_idx < 4; ++v_idx) {
        std::uint32_t v_h = 0;
        CHECK(sceNgs2RackGetVoiceHandle(rack_h, v_idx, &v_h) == 0);
        float high_vol = 5.0f; // Loud to trigger clamping
        CHECK(sceNgs2VoiceControl(v_h, SCE_NGS2_VOICE_COMMAND_SET_VOLUME, &high_vol) == 0);
        CHECK(sceNgs2VoiceControl(v_h, SCE_NGS2_VOICE_COMMAND_PLAY, nullptr) == 0);
    }
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 1024) == 0);
    for (auto s : pcm_buf) {
        CHECK(s >= -32768 && s <= 32767);
    }

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

    // 1. Set attributes with position (X=10, Y=20, Z=-5) and Gain=1.0
    float coords[4] = { 10.0f, 20.0f, -5.0f, 1.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, coords) == 0);

    // 2. Read back attributes
    float read_attrs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    CHECK(sceAudio3dObjectGetAttributes(port_h, obj_h, read_attrs) == 0);
    CHECK(std::abs(read_attrs[0] - 10.0f) < 1e-4f);
    CHECK(std::abs(read_attrs[1] - 20.0f) < 1e-4f);
    CHECK(std::abs(read_attrs[2] - (-5.0f)) < 1e-4f);
    CHECK(std::abs(read_attrs[3] - 1.0f) < 1e-4f);

    // 3. Listener setup
    Audio3DVec3 listener_pos{ 0.0f, 0.0f, 0.0f };
    Audio3DVec3 forward{ 0.0f, 0.0f, 1.0f };
    Audio3DVec3 up{ 0.0f, 1.0f, 0.0f };
    set_audio3d_listener(listener_pos, forward, up);

    // 4. Center object at (0, 0, 10): balanced stereo gains
    float center_pos[4] = { 0.0f, 0.0f, 10.0f, 1.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, center_pos) == 0);
    float gain_l = 0.0f, gain_r = 0.0f;
    CHECK(compute_audio3d_spatial_gains(port_h, obj_h, gain_l, gain_r));
    CHECK(gain_l > 0.0f);
    CHECK(std::abs(gain_l - gain_r) < 1e-4f);

    // 5. Hard left object at (-10, 0, 0): left gain > 0, right gain == 0
    float left_pos[4] = { -10.0f, 0.0f, 0.0f, 1.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, left_pos) == 0);
    CHECK(compute_audio3d_spatial_gains(port_h, obj_h, gain_l, gain_r));
    CHECK(gain_l > 0.0f);
    CHECK(gain_r < 1e-4f);

    // 6. Hard right object at (10, 0, 0): left gain == 0, right gain > 0
    float right_pos[4] = { 10.0f, 0.0f, 0.0f, 1.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, right_pos) == 0);
    CHECK(compute_audio3d_spatial_gains(port_h, obj_h, gain_l, gain_r));
    CHECK(gain_l < 1e-4f);
    CHECK(gain_r > 0.0f);

    // 7. Distant object at (0, 0, 200) beyond max_dist (100): gain == 0
    float far_pos[4] = { 0.0f, 0.0f, 200.0f, 1.0f };
    CHECK(sceAudio3dObjectSetAttributes(port_h, obj_h, far_pos) == 0);
    CHECK(compute_audio3d_spatial_gains(port_h, obj_h, gain_l, gain_r));
    CHECK(gain_l == 0.0f && gain_r == 0.0f);

    CHECK(sceAudio3dPortFlush(port_h) == 0);
    CHECK(sceAudio3dObjectUnreserve(port_h, obj_h) == 0);
    CHECK(sceAudio3dPortClose(port_h) == 0);
    CHECK(sceAudio3dTerminate() == 0);
    std::printf("[PASS] audio3d_lifecycle\n");
}

static void test_ngs2_3d_integration() {
    std::uint32_t sys_h = 0;
    CHECK(sceNgs2SystemCreateWithAllocator(nullptr, nullptr, &sys_h) == 0);
    std::uint32_t rack_h = 0;
    CHECK(sceNgs2RackCreateWithAllocator(sys_h, nullptr, nullptr, &rack_h) == 0);
    std::uint32_t voice_h = 0;
    CHECK(sceNgs2RackGetVoiceHandle(rack_h, 0, &voice_h) == 0);

    // Listener at origin
    set_audio3d_listener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});

    // Voice positioned at hard left (-10, 0, 0)
    float pos_left[3] = { -10.0f, 0.0f, 0.0f };
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_SET_3D_POS, pos_left) == 0);
    CHECK(sceNgs2VoiceControl(voice_h, SCE_NGS2_VOICE_COMMAND_PLAY, nullptr) == 0);

    std::vector<std::int16_t> pcm_buf(256 * 2, 0);
    CHECK(sceNgs2SystemRender(sys_h, pcm_buf.data(), 256) == 0);

    // Right channel should be silent, left channel active
    bool left_active = false;
    for (std::size_t i = 0; i < 256; ++i) {
        CHECK(pcm_buf[i * 2 + 1] == 0);
        if (pcm_buf[i * 2 + 0] != 0) left_active = true;
    }
    CHECK(left_active);

    CHECK(sceNgs2RackDestroy(rack_h) == 0);
    CHECK(sceNgs2SystemDestroy(sys_h) == 0);
    std::printf("[PASS] ngs2_3d_integration\n");
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view suite(argv[1]);
        if (suite == "ngs2") { test_ngs2(); return 0; }
        if (suite == "audio3d") { test_audio3d(); return 0; }
        if (suite == "integration") { test_ngs2_3d_integration(); return 0; }
    }
    test_ngs2();
    test_audio3d();
    test_ngs2_3d_integration();
    return 0;
}
