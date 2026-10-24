#include "hle/audio/audio3d.h"
#include "hle/guest_memory.h"
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace gravity::hle::audio {

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_AUDIO3D_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x805C0001U);

struct PortInstance {
    std::uint32_t handle{0};
    std::unordered_set<std::uint32_t> objects;
};

std::mutex g_audio3d_mutex;
bool g_audio3d_initialized{false};
std::unordered_map<std::uint32_t, PortInstance> g_ports;
std::uint32_t g_next_port = 1;
std::uint32_t g_next_obj = 1;

} // namespace

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceAudio3dInitialize(const void* params) {
    (void)params;
    std::lock_guard lock(g_audio3d_mutex);
    g_audio3d_initialized = true;
    std::fprintf(stderr, "[Audio3D] sceAudio3dInitialize (SCE_OK)\n");
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dTerminate() {
    std::lock_guard lock(g_audio3d_mutex);
    g_ports.clear();
    g_audio3d_initialized = false;
    std::fprintf(stderr, "[Audio3D] sceAudio3dTerminate (SCE_OK)\n");
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortOpen(const void* params, std::uint32_t* out_port_handle) {
    (void)params;
    if (!out_port_handle || !guest_memory::accessible(out_port_handle, sizeof(std::uint32_t), true)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_audio3d_mutex);
    if (!g_audio3d_initialized) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    const std::uint32_t handle = g_next_port++;
    PortInstance inst;
    inst.handle = handle;
    g_ports[handle] = std::move(inst);
    guest_memory::write(out_port_handle, &handle, sizeof(handle));
    std::fprintf(stderr, "[Audio3D] sceAudio3dPortOpen: port=%u\n", handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortClose(std::uint32_t port_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    g_ports.erase(it);
    std::fprintf(stderr, "[Audio3D] sceAudio3dPortClose: port=%u\n", port_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortFlush(std::uint32_t port_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    if (!g_ports.contains(port_handle)) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectReserve(std::uint32_t port_handle, std::uint32_t* out_obj_handle) {
    if (!out_obj_handle || !guest_memory::accessible(out_obj_handle, sizeof(std::uint32_t), true)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;

    const std::uint32_t obj = g_next_obj++;
    it->second.objects.insert(obj);
    guest_memory::write(out_obj_handle, &obj, sizeof(obj));
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectUnreserve(std::uint32_t port_handle, std::uint32_t obj_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    it->second.objects.erase(obj_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectSetAttributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, const void* attrs) {
    (void)attrs;
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end() || !it->second.objects.contains(obj_handle)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::audio
