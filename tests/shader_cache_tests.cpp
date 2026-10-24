#include "hle/gnm/shader_cache.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using gravity::hle::gnm::ShaderCache;

namespace {

constexpr std::array<std::uint8_t, 8> source{0x00, 0x03, 0x00, 0xbe, 0x00, 0x00, 0x81, 0xbf};
constexpr std::string_view options = "translator=1;stage=compute;target=vulkan1.0;layout=none";
// A complete SPIR-V 1.0 compute shader with an empty main().
const std::vector<std::uint32_t> module{
    0x07230203U, 0x00010000U, 0, 5, 0,
    0x00020011U, 1,
    0x0003000eU, 0, 1,
    0x0005000fU, 5, 3, 0x6e69616dU, 0,
    0x00060010U, 3, 17, 1, 1, 1,
    0x00020013U, 1,
    0x00030021U, 2, 1,
    0x00050036U, 1, 3, 0, 2,
    0x000200f8U, 4,
    0x000100fdU,
    0x00010038U};

struct Fixture {
    std::filesystem::path path;
    ShaderCache cache;
    Fixture() : path(make_path()), cache(path) {}
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    static std::filesystem::path make_path() {
        static std::atomic<unsigned> serial{};
        return std::filesystem::temp_directory_path() /
               ("gravity_shader_cache_" + std::to_string(GetCurrentProcessId()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(serial.fetch_add(1)));
    }
    std::filesystem::path entry(std::string_view key) const { return path / (std::string(key) + ".spv"); }
};

std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void write_bytes(const std::filesystem::path& path, std::span<const char> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    CHECK(file.good());
}
void check_no_temporary(const std::filesystem::path& path) {
    for (const auto& entry : std::filesystem::directory_iterator(path)) CHECK(entry.path().extension() != ".tmp");
}

void roundtrip() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    CHECK(key.size() == 64);
    CHECK(fixture.cache.store(key, module));
    CHECK(fixture.cache.load(key) == module);
    // A fresh cache object reads persistent data without in-memory state.
    CHECK(ShaderCache(fixture.path).load(key) == module);
    auto second = module;
    second[2] = 12345; // Legal generator ID.
    CHECK(fixture.cache.store(key, second));
    CHECK(fixture.cache.load(key) == second);
    check_no_temporary(fixture.path);
}
void miss() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    CHECK(!fixture.cache.load(key));
    CHECK(!std::filesystem::exists(fixture.path));
    CHECK(fixture.cache.store(key, module));
    CHECK(!fixture.cache.load(ShaderCache::key_for(source, "translator=2")));
    CHECK(!fixture.cache.load("../../unexpected"));
    CHECK(!fixture.cache.load(std::string(64, 'G')));
}
void identity() {
    const auto original = ShaderCache::key_for(source, options);
    CHECK(original.size() == 64);
    CHECK(original == ShaderCache::key_for(source, options));
    CHECK(original == ShaderCache::key_for(std::as_bytes(std::span(source)), options));
    auto changed = source;
    changed.back() ^= 1;
    CHECK(original != ShaderCache::key_for(changed, options));
    CHECK(original != ShaderCache::key_for(std::span(source).first(7), options));
    for (const auto other : {"translator=2;stage=compute;target=vulkan1.0;layout=none",
                             "translator=1;stage=vertex;target=vulkan1.0;layout=none",
                             "translator=1;stage=compute;target=vulkan1.1;layout=none",
                             "translator=1;stage=compute;target=vulkan1.0;layout=storage0"})
        CHECK(original != ShaderCache::key_for(source, other));
    constexpr std::array<std::uint8_t, 1> one{1};
    constexpr std::array<std::uint8_t, 2> two{1, 'a'};
    CHECK(ShaderCache::key_for(one, "ab") != ShaderCache::key_for(two, "b"));
    CHECK(ShaderCache::key_for(source, std::string_view("x\0y", 3)) != ShaderCache::key_for(source, "x"));
}
void corruption() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    CHECK(fixture.cache.store(key, module));
    const auto valid = read_bytes(fixture.entry(key));
    // Cover magic, version, header size, payload length, key, checksum and body.
    for (const std::size_t offset : {0U, 8U, 12U, 16U, 24U, 56U, 88U, 96U}) {
        auto damaged = valid;
        damaged[offset] ^= 0x40;
        write_bytes(fixture.entry(key), damaged);
        CHECK(!fixture.cache.load(key));
    }
    auto damaged = valid;
    damaged.push_back('x');
    write_bytes(fixture.entry(key), damaged);
    CHECK(!fixture.cache.load(key));
    // A valid entry copied under a different key is still a miss.
    const auto other_key = ShaderCache::key_for(source, "translator=other");
    write_bytes(fixture.entry(other_key), valid);
    CHECK(!fixture.cache.load(other_key));
    CHECK(fixture.cache.store(key, module));
    CHECK(fixture.cache.load(key) == module);
}
void truncation() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    CHECK(fixture.cache.store(key, module));
    const auto valid = read_bytes(fixture.entry(key));
    for (std::size_t size = 0; size < valid.size(); ++size) {
        write_bytes(fixture.entry(key), std::span(valid).first(size));
        CHECK(!fixture.cache.load(key));
    }
    auto oversized = valid;
    for (std::size_t i = 16; i < 24; ++i) oversized[i] = static_cast<char>(0xff);
    write_bytes(fixture.entry(key), oversized);
    CHECK(!fixture.cache.load(key));
}
void concurrent() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    // Independent writers must also converge when the directory does not yet
    // exist, rather than depending on a preparatory store by one thread.
    std::atomic<unsigned> first_failures{};
    std::vector<std::thread> first_writers;
    for (unsigned thread = 0; thread < 8; ++thread) first_writers.emplace_back([&] {
        if (!ShaderCache(fixture.path).store(key, module)) ++first_failures;
    });
    for (auto& writer : first_writers) writer.join();
    CHECK(first_failures == 0);
    CHECK(fixture.cache.load(key) == module);
    std::atomic<bool> done{};
    std::atomic<unsigned> failures{};
    std::thread reader([&] {
        while (!done.load()) {
            const auto words = fixture.cache.load(key);
            if (!words || words->size() != module.size() || (*words)[2] >= 8) { ++failures; continue; }
            auto expected = module;
            expected[2] = (*words)[2];
            if (*words != expected) ++failures;
        }
    });
    std::vector<std::thread> writers;
    for (unsigned thread = 0; thread < 8; ++thread) writers.emplace_back([&, thread] {
        ShaderCache independent(fixture.path);
        auto words = module;
        words[2] = thread;
        for (unsigned iteration = 0; iteration < 12; ++iteration)
            if (!independent.store(key, words)) ++failures;
    });
    for (auto& writer : writers) writer.join();
    done = true;
    reader.join();
    CHECK(failures == 0);
    const auto final = fixture.cache.load(key);
    CHECK(final && final->size() == module.size() && (*final)[2] < 8);
    auto expected = module;
    expected[2] = (*final)[2];
    CHECK(*final == expected);
    check_no_temporary(fixture.path);
}
void invalid() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    CHECK(!fixture.cache.store("../escape", module));
    CHECK(!fixture.cache.store(key, {}));
    CHECK(!fixture.cache.store(key, std::span(module).first(4)));
    const auto reject = [&](std::size_t offset, std::uint32_t replacement) {
        auto invalid_words = module;
        invalid_words[offset] = replacement;
        CHECK(!fixture.cache.store(key, invalid_words));
    };
    reject(0, 0x03022307U);
    reject(1, 0x00010700U);
    reject(1, 0x00010001U);
    reject(3, 0);
    reject(3, 0x00400000U);
    reject(4, 1);
    reject(5, 17); // Zero instruction word count.
    reject(5, 0xffff0011U); // Instruction extends beyond file.
    CHECK(!std::filesystem::exists(fixture.path));
    auto huge = module;
    huge.resize(ShaderCache::max_spirv_bytes / 4 + 1);
    CHECK(!fixture.cache.store(key, huge));
}
void failures() {
    Fixture fixture;
    const auto key = ShaderCache::key_for(source, options);
    std::filesystem::create_directories(fixture.path);
    // A directory at the destination makes the atomic replacement fail after
    // the temporary file has been written, exercising failure cleanup.
    std::filesystem::create_directory(fixture.entry(key));
    CHECK(!fixture.cache.store(key, module));
    CHECK(!fixture.cache.load(key));
    check_no_temporary(fixture.path);
    const auto blocked = fixture.path / "file_instead_of_directory";
    write_bytes(blocked, std::span<const char>("x", 1));
    CHECK(!ShaderCache(blocked).store(key, module));
    CHECK(!ShaderCache(blocked).load(key));
    check_no_temporary(fixture.path);
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view suite = argc > 1 ? argv[1] : "all";
    bool recognized = false;
    for (const auto& [name, run] : std::array<std::pair<std::string_view, void (*)()>, 8>{{
             {"roundtrip", roundtrip}, {"miss", miss}, {"identity", identity}, {"corruption", corruption},
             {"truncation", truncation}, {"concurrent", concurrent}, {"invalid", invalid}, {"failures", failures}}}) {
        if (suite == "all" || suite == name) { run(); recognized = true; }
    }
    CHECK(recognized);
    std::printf("shader_cache_%.*s: OK\n", static_cast<int>(suite.size()), suite.data());
}
