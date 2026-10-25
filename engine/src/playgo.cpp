#include "hle/playgo.h"
#include "hle/guest_memory.h"
#include <cstdio>
#include <mutex>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace gravity::hle::playgo {

namespace {
std::mutex g_playgo_mutex;
bool g_initialized{false};
std::int32_t g_next_handle{1};
std::unordered_set<std::int32_t> g_open_handles{};
} // anonymous namespace

bool is_initialized() noexcept {
    std::lock_guard lock(g_playgo_mutex);
    return g_initialized;
}

void reset() noexcept {
    std::lock_guard lock(g_playgo_mutex);
    g_initialized = false;
    g_next_handle = 1;
    g_open_handles.clear();
}

extern "C" {

GRAVITY_SYSV_ABI std::int32_t scePlayGoInitialize(const ScePlayGoInitParams* params) {
    if (!params) {
        std::fprintf(stderr, "[PlayGo] scePlayGoInitialize: NULL params\n");
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    if (!guest_memory::accessible(params, sizeof(ScePlayGoInitParams))) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }

    std::lock_guard lock(g_playgo_mutex);
    g_initialized = true;
    std::fprintf(stderr, "[PlayGo] Initialized (buffer_size=%u)\n", params->buffer_size);
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoTerminate() {
    std::lock_guard lock(g_playgo_mutex);
    g_initialized = false;
    g_open_handles.clear();
    std::fprintf(stderr, "[PlayGo] Terminated\n");
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoOpen(std::int32_t* out_handle, const void* reserved) {
    (void)reserved;
    if (!out_handle) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;

    std::lock_guard lock(g_playgo_mutex);
    const std::int32_t handle = g_next_handle++;
    g_open_handles.insert(handle);

    if (!guest_memory::write(out_handle, &handle, sizeof(handle))) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }

    std::fprintf(stderr, "[PlayGo] Opened handle=%d\n", handle);
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoClose(std::int32_t handle) {
    std::lock_guard lock(g_playgo_mutex);
    g_open_handles.erase(handle);
    std::fprintf(stderr, "[PlayGo] Closed handle=%d\n", handle);
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoGetToDoList(
    std::int32_t handle, void* out_chunks, std::uint32_t max_count, std::uint32_t* out_count) {
    (void)handle; (void)out_chunks; (void)max_count;
    if (!out_count) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;

    // The game is 100% installed from local files: to-do list count is 0
    const std::uint32_t count = 0;
    if (!guest_memory::write(out_count, &count, sizeof(count))) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }

    std::fprintf(stderr, "[PlayGo] GetToDoList: count=0 (fully installed)\n");
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoGetInstallSpeed(std::int32_t handle, std::uint32_t* out_speed) {
    (void)handle;
    if (!out_speed) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;

    const std::uint32_t speed = 100000000; // 100 MB/s instantaneous
    if (!guest_memory::write(out_speed, &speed, sizeof(speed))) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoSetInstallSpeed(std::int32_t handle, std::uint32_t speed) {
    (void)handle; (void)speed;
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoGetLocus(
    std::int32_t handle, const std::uint16_t* chunk_ids, std::uint32_t count, std::uint8_t* out_locus) {
    (void)handle;
    (void)chunk_ids;
    if (!out_locus || count == 0) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    if (!guest_memory::accessible(out_locus, count, true)) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;

    std::vector<std::uint8_t> loci(count, static_cast<std::uint8_t>(SCE_PLAYGO_LOCUS_LOCAL));
    if (!guest_memory::write(out_locus, loci.data(), count)) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    std::fprintf(stderr, "[PlayGo] GetLocus: returned %u chunks as LOCAL (%u)\n", count, SCE_PLAYGO_LOCUS_LOCAL);
    return SCE_PLAYGO_OK;
}

GRAVITY_SYSV_ABI std::int32_t scePlayGoGetEta(std::int32_t handle, std::uint32_t* out_eta) {
    (void)handle;
    if (!out_eta) return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;

    const std::uint32_t eta = 0; // 0 seconds remaining
    if (!guest_memory::write(out_eta, &eta, sizeof(eta))) {
        return SCE_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    return SCE_PLAYGO_OK;
}

} // extern "C"

} // namespace gravity::hle::playgo
