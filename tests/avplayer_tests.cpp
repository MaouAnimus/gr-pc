#include "hle/avplayer/avplayer.h"
#include "hle/fs.h"
#include "avplayer_fixture.h"
#include "../engine/src/avplayer/mf_decoder.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::avplayer;
using Clock = std::chrono::steady_clock;
constexpr std::int32_t no_data = static_cast<std::int32_t>(0x80540002U);
void avplayer_vulkan_test(SceAvPlayerHandle handle);

void raw_video_archive(const std::filesystem::path& root) {
    std::ifstream file(root / "fixture.mp4", std::ios::binary | std::ios::ate);
    CHECK(file.is_open()); const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint8_t> movie(size); file.seekg(0); CHECK(file.read(reinterpret_cast<char*>(movie.data()), size));
    constexpr std::size_t block = 65536;
    const std::string manifest = "mp4/fixture.mp4\n";
    const auto blocks = (movie.size() + block - 1) / block;
    const auto toc = 32 + 60 + 2 * (blocks + 1);
    std::vector<std::uint8_t> archive(toc + manifest.size() + movie.size());
    auto be = [&](std::size_t offset, std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) archive[offset + bytes - i - 1] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    std::memcpy(archive.data(), "PSAR", 4); be(4, 0x10004, 4); std::memcpy(archive.data() + 8, "zlib", 4);
    be(12, toc, 4); be(16, 30, 4); be(20, 2, 4); be(24, block, 4);
    be(32 + 16, 0, 4); be(32 + 20, manifest.size(), 5); be(32 + 25, toc, 5);
    be(62 + 16, 1, 4); be(62 + 20, movie.size(), 5); be(62 + 25, toc + manifest.size(), 5);
    be(92, manifest.size(), 2);
    for (std::size_t i = 0; i < blocks; ++i) {
        const auto length = (std::min)(block, movie.size() - i * block);
        be(94 + i * 2, length == block ? 0 : length, 2);
    }
    std::memcpy(archive.data() + toc, manifest.data(), manifest.size());
    std::memcpy(archive.data() + toc + manifest.size(), movie.data(), movie.size());
    std::filesystem::create_directories(root / "arc");
    std::ofstream out(root / "arc" / "video.psarc", std::ios::binary);
    CHECK(out.write(reinterpret_cast<const char*>(archive.data()), archive.size()));
}

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("gravity_avplayer_" + std::to_string(GetCurrentProcessId()));
    Fixture(bool audio = true, std::uint32_t width = 64, std::uint32_t height = 48, std::uint32_t frames = 12, bool archive = false) {
        std::filesystem::create_directories(root);
        create_avplayer_fixture(root / "fixture.mp4", width, height, frames, audio);
        if (archive) raw_video_archive(root);
        gravity::hle::fs::initialize_vfs(root.wstring());
        gravity::hle::fs::vfs_mount_path("/avtest", root);
    }
    ~Fixture() {
        gravity::hle::fs::shutdown_vfs();
        std::filesystem::remove_all(root);
    }
};
SceAvPlayerFrameInfoEx next_frame(SceAvPlayerHandle player) {
    SceAvPlayerFrameInfoEx frame{};
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    for (;;) {
        const auto status = sceAvPlayerGetVideoDataEx(player, &frame);
        if (!status) return frame;
        CHECK(status == no_data && Clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void check_colors(const SceAvPlayerFrameInfoEx& frame, unsigned index) {
    const auto* top = static_cast<const std::uint8_t*>(frame.pData) + (frame.height / 4 * frame.width + frame.width / 2) * 4;
    const auto* bottom = top + frame.height / 2 * frame.pitch;
    CHECK(top[3] == 255 && bottom[3] == 255);
    if (!(index % 2)) { CHECK(top[0] > 180 && top[2] < 70); CHECK(bottom[2] > 180 && bottom[0] < 70); }
    else { CHECK(top[2] > 180 && top[0] < 70); CHECK(bottom[0] > 180 && bottom[2] < 70); }
}
void lifecycle() {
    auto p = sceAvPlayerInit(nullptr); CHECK(p);
    CHECK(sceAvPlayerPostInit(p, nullptr) == 0); CHECK(sceAvPlayerStreamCount(p) == 0);
    CHECK(sceAvPlayerStart(p) != 0); CHECK(sceAvPlayerResume(p) != 0);
    CHECK(sceAvPlayerIsActive(p) == 0); CHECK(sceAvPlayerClose(p) == 0); CHECK(sceAvPlayerClose(p) != 0);
    CHECK(sceAvPlayerInit(reinterpret_cast<SceAvPlayerInitData*>(1)) == nullptr);
}
void invalid_inputs() {
    Fixture fixture;
    auto p = sceAvPlayerInit(nullptr); CHECK(p);
    CHECK(sceAvPlayerAddSource(p, nullptr) != 0); CHECK(sceAvPlayerAddSource(p, reinterpret_cast<const char*>(1)) != 0);
    CHECK(sceAvPlayerAddSource(p, "/avtest/missing.mp4") != 0);
    std::ofstream(fixture.root / "bad.mp4") << "not a movie";
    CHECK(sceAvPlayerAddSource(p, "/avtest/bad.mp4") != 0);
    CHECK(sceAvPlayerStreamCount(p) == 0);
    CHECK(sceAvPlayerAddSource(p, "/avtest/fixture.mp4") == 0);
    CHECK(sceAvPlayerAddSource(p, "/avtest/missing.mp4") != 0); // prior valid source survives
    CHECK(sceAvPlayerStreamCount(p) == 2);
    SceAvPlayerStreamInfo stream{}; SceAvPlayerFrameInfoEx frame{};
    CHECK(sceAvPlayerGetStreamInfo(p, 2, &stream) != 0); CHECK(sceAvPlayerEnableStream(p, 2) != 0);
    CHECK(sceAvPlayerGetStreamInfo(p, 0, reinterpret_cast<SceAvPlayerStreamInfo*>(1)) != 0);
    CHECK(sceAvPlayerGetVideoDataEx(p, reinterpret_cast<SceAvPlayerFrameInfoEx*>(1)) != 0);
    CHECK(sceAvPlayerGetAudioData(p, nullptr) != 0); CHECK(sceAvPlayerGetVideoDataEx(p, &frame) == no_data);
    CHECK(sceAvPlayerClose(p) == 0);
}
void playback(bool archive = false) {
    Fixture fixture(true, 64, 48, 12, archive);
    auto p = sceAvPlayerInit(nullptr); CHECK(p);
    CHECK(sceAvPlayerAddSource(p, archive ? "/app0/mp4/fixture.mp4" : "/app0/fixture.mp4") == 0);
    CHECK(sceAvPlayerStreamCount(p) == 2);
    SceAvPlayerStreamInfo stream{};
    CHECK(sceAvPlayerGetStreamInfo(p, 0, &stream) == 0);
    CHECK(stream.type == 0 && stream.width == 64 && stream.height == 48 && stream.framerate == 60);
    CHECK(sceAvPlayerGetStreamInfo(p, 1, &stream) == 0);
    CHECK(stream.type == 1 && stream.channelCount == 2 && stream.sampleRate == 48000);
    CHECK(sceAvPlayerStart(p) == 0);
    CHECK(sceAvPlayerAddSource(p, "/avtest/fixture.mp4") != 0);
    auto frame = next_frame(p); check_colors(frame, 0);
    CHECK(frame.timestamp == 0 && frame.pitch == 256 && frame.pixelFormat == 1);
    CHECK(reinterpret_cast<std::uintptr_t>(frame.pData) % 256 == 0);
    auto snapshot = video_texture_frames(); CHECK(snapshot.size() == 1);
    CHECK(snapshot[0].address == reinterpret_cast<std::uintptr_t>(frame.pData));
    const auto first_pixels = *snapshot[0].rgba;
    CHECK(sceAvPlayerPause(p) == 0); CHECK(!sceAvPlayerIsActive(p));
    const auto before_pause = frame;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(sceAvPlayerGetVideoDataEx(p, &frame) == no_data); CHECK(frame.pData == before_pause.pData);
    CHECK(sceAvPlayerResume(p) == 0);
    unsigned videos = 1, audios = 0; bool nonzero_audio = false;
    auto last_pts = frame.timestamp;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (sceAvPlayerIsActive(p)) {
        CHECK(Clock::now() < deadline);
        auto status = sceAvPlayerGetVideoDataEx(p, &frame);
        if (!status) { CHECK(frame.timestamp > last_pts); last_pts = frame.timestamp; check_colors(frame, videos++); }
        else CHECK(status == no_data);
        SceAvPlayerAudioInfo audio{};
        status = sceAvPlayerGetAudioData(p, &audio);
        if (!status) {
            ++audios; CHECK(audio.size && audio.channelCount == 2 && audio.sampleRate == 48000);
            const auto* bytes = static_cast<const std::uint8_t*>(audio.pData);
            nonzero_audio |= std::any_of(bytes, bytes + audio.size, [](auto value) { return value != 0; });
        } else CHECK(status == no_data);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(videos == 12 && audios > 0 && nonzero_audio);
    CHECK(*snapshot[0].rgba == first_pixels); // renderer snapshot stays immutable
    CHECK(sceAvPlayerStop(p) == 0); CHECK(sceAvPlayerStart(p) == 0);
    frame = next_frame(p); CHECK(frame.timestamp == 0); check_colors(frame, 0);
    CHECK(sceAvPlayerClose(p) == 0); CHECK(video_texture_frames().empty());
}
void looping() {
    Fixture fixture(false, 64, 48, 3);
    auto p = sceAvPlayerInit(nullptr); CHECK(p);
    CHECK(sceAvPlayerAddSource(p, "/avtest/fixture.mp4") == 0); CHECK(sceAvPlayerStreamCount(p) == 1);
    CHECK(sceAvPlayerSetLooping(p, 1) == 0); CHECK(sceAvPlayerStart(p) == 0);
    for (unsigned i = 0; i < 9; ++i) { const auto frame = next_frame(p); check_colors(frame, i % 3); }
    CHECK(sceAvPlayerStop(p) == 0); CHECK(!sceAvPlayerIsActive(p)); CHECK(sceAvPlayerClose(p) == 0);
}
void performance() {
    Fixture fixture(false, 1920, 1080, 120);
    MediaDecoder decoder; MediaMetadata metadata;
    CHECK(decoder.open("/avtest/fixture.mp4", metadata));
    CHECK(metadata.width == 1920 && metadata.height == 1080 && metadata.frame_rate == 60);
    const auto start = Clock::now(); unsigned frames = 0;
    for (;;) {
        auto sample = decoder.read(true).get();
        if (sample.status == MediaSample::Status::end) break;
        if (sample.status == MediaSample::Status::again) continue;
        CHECK(sample.status == MediaSample::Status::data && sample.bytes.size() == 1920 * 1080 * 4);
        ++frames;
    }
    CHECK(frames == 120);
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::printf("H.264 1080p60 -> RGBA: %u frames, %.3f s, %.1f fps (decode throughput; hardware-dependent)\n", frames, seconds, frames / seconds);
}
// Optional local-content probe. Deliberately excluded from hermetic CTest runs.
int content(const std::filesystem::path& root) {
    std::vector<std::string> movies;
    if (!std::filesystem::exists(root / "arc")) { std::puts("SKIP: game archives unavailable"); return 77; }
    for (const auto& file : std::filesystem::directory_iterator(root / "arc")) {
        if (file.path().extension() != ".psarc") continue;
        gravity::hle::fs::PsarcArchive archive;
        CHECK(archive.open(file.path().wstring()));
        for (const auto& entry : archive.entries()) {
            const auto extension = std::filesystem::path(entry.path).extension();
            if (extension == ".mp4" || extension == ".m4v" || extension == ".pam" || extension == ".webm") movies.push_back(entry.path);
        }
    }
    std::printf("Game manifest: %zu candidate video files\n", movies.size());
    if (movies.empty()) return 77;
    std::stable_sort(movies.begin(), movies.end(), [](const auto& a, const auto& b) {
        return (a == "mp4/ep00-01c.mp4") > (b == "mp4/ep00-01c.mp4");
    });
    gravity::hle::fs::initialize_vfs(root.wstring());
    for (const auto& movie : movies) {
        MediaDecoder decoder; MediaMetadata metadata;
        const auto path = "/app0/" + movie;
        if (!decoder.open(path, metadata)) { std::printf("Unsupported media: %s\n", path.c_str()); continue; }
        std::printf("Media: %s, %ux%u, %.3f fps\n", path.c_str(), metadata.width, metadata.height, metadata.frame_rate);
        if (!metadata.video) continue;
        const auto start = Clock::now(); unsigned frames{};
        for (unsigned attempt = 0; attempt < 140; ++attempt) {
            const auto sample = decoder.read(true).get();
            if (sample.status == MediaSample::Status::again) continue;
            if (sample.status == MediaSample::Status::end && frames) break;
            CHECK(sample.status == MediaSample::Status::data && !sample.bytes.empty());
            if (++frames == 120) break;
        }
        CHECK(frames);
        const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        std::printf("Game media decoded through VFS/Media Foundation: %u frames, %.3f s, %.1f fps\n", frames, elapsed, frames / elapsed);
        return 0;
    }
    return 1;
}
int main(int argc, char** argv) {
    const std::string_view suite = argc > 1 ? argv[1] : "lifecycle";
    if (suite == "lifecycle") lifecycle();
    else if (suite == "invalid") invalid_inputs();
    else if (suite == "playback") playback();
    else if (suite == "psarc") playback(true);
    else if (suite == "looping") looping();
    else if (suite == "performance") performance();
    else if (suite == "content") return content(argc > 2 ? argv[2] : "../../CUSA01130");
    else if (suite == "vulkan") {
        Fixture fixture(false, 1920, 1080, 120);
        auto p = sceAvPlayerInit(nullptr); CHECK(p);
        CHECK(sceAvPlayerAddSource(p, "/avtest/fixture.mp4") == 0);
        avplayer_vulkan_test(p);
        CHECK(sceAvPlayerClose(p) == 0);
    } else return 1;
    std::printf("[PASS] avplayer_%.*s\n", static_cast<int>(suite.size()), suite.data());
}
