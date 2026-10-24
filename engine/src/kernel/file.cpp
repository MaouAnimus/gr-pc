#include "hle/kernel/file.h"
#include "hle/guest_memory.h"
#include "hle/fs.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <string_view>

namespace gravity::hle::kernel_file {
namespace {
std::mutex files_mutex;
std::set<std::int32_t> descriptors;
constexpr auto bad_fd = static_cast<std::int32_t>(0x80020009U);
constexpr auto fault = static_cast<std::int32_t>(0x8002000eU);
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelOpen(const char* path, std::int32_t flags, std::uint16_t mode) {
    char safe_path[1024]{};
    if (!guest_memory::string(path, safe_path, sizeof(safe_path))) return fault;
    std::fprintf(stderr, "[File] Open %s flags=0x%x\n", safe_path, flags);
    if ((flags & 3) || (flags & (0x200 | 0x400 | 0x800)))
        return static_cast<std::int32_t>(0x8002001eU); // EROFS; save-data mount is not implemented here.
    if (flags) return static_cast<std::int32_t>(0x8002002dU); // Unsupported flags.
    const std::string_view view(safe_path);
    if (view.starts_with('/') && !view.starts_with("/app0/"))
        return static_cast<std::int32_t>(0x80020002U);
    std::lock_guard lock(files_mutex);
    const auto fd = fs::sceFios2Open(safe_path, flags, mode);
    if (fd < 0) return static_cast<std::int32_t>(0x80020002U);
    descriptors.insert(fd);
    return fd;
}
extern "C" GRAVITY_SYSV_ABI std::int64_t sceKernelRead(std::int32_t fd, void* buffer, std::size_t size) {
    std::lock_guard lock(files_mutex);
    if (!descriptors.contains(fd)) return bad_fd;
    if (!size) return 0;
    const auto bounded = (std::min)(size, static_cast<std::size_t>(INT32_MAX));
    if (!guest_memory::accessible(buffer, bounded, true)) return fault;
    const auto result = fs::sceFios2Read(fd, buffer, static_cast<std::uint32_t>(bounded));
    return result < 0 ? static_cast<std::int32_t>(0x80020005U) : result;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelClose(std::int32_t fd) {
    std::lock_guard lock(files_mutex);
    if (!descriptors.contains(fd)) return bad_fd;
    const auto result = fs::sceFios2Close(fd);
    descriptors.erase(fd);
    return result < 0 ? static_cast<std::int32_t>(0x80020005U) : 0;
}
}
