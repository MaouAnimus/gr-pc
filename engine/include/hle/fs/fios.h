#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::fios {

// Authentic Sony Orbis SceFiosParams layout (152 bytes / 0x98)
struct alignas(8) SceFiosParams {
    std::uint32_t size{0};            // Bits 1..15 encode struct size: ((size >> 1) & 0x7FFF) == 0x98
    std::uint32_t flags{0};
    std::uint32_t thread_count{0};    // e.g. 2
    std::uint32_t extra_threads{0};   // e.g. 1
    std::uint32_t reserved1{0};
    std::uint32_t stack_size{0};      // e.g. 0x80000 (512 KiB)
    std::uint32_t profile_mode{0};    // e.g. 2
    std::uint32_t reserved2{0};
    std::uint64_t unk20{0};
    std::uint64_t unk28{0};
    void*         buffer30{nullptr};  // Working buffer 1 (e.g. 0x1d80e60)
    std::uint64_t size38{0};          // Size 1 (e.g. 0x3f040)
    void*         buffer40{nullptr};  // Working buffer 2 (e.g. 0x1e602a0)
    std::uint64_t size48{0};          // Size 2 (e.g. 0x42060)
    void*         buffer50{nullptr};  // Working buffer 3 (e.g. 0x1ea2300)
    std::uint64_t size58{0};          // Size 3 (e.g. 0x10e8)
    void*         buffer60{nullptr};  // Working buffer 4 (e.g. 0x1dbfea0)
    std::uint64_t size68{0};          // Size 4 (e.g. 0xa0400)
    void*         buffer70{nullptr};
    void*         allocator_cb{nullptr};
    std::uint64_t unk80{0};
    std::uint32_t thread_priority_io{0};      // e.g. 0x2ae (686)
    std::uint32_t thread_priority_worker{0};  // e.g. 0x2b0 (688)
    std::uint32_t unk90{0};
    std::uint32_t unk94{0};
};

static_assert(sizeof(SceFiosParams) == 0x98, "SceFiosParams must be 152 bytes (0x98)");

// Error codes matching libSceFios2 authentic disassembly:
constexpr std::int32_t SCE_FIOS_OK                        = 0;
constexpr std::int32_t SCE_FIOS_ERROR_ALREADY_INITIALIZED = static_cast<std::int32_t>(0x80820001);
constexpr std::int32_t SCE_FIOS_ERROR_NOT_INITIALIZED     = static_cast<std::int32_t>(0x80820002);
constexpr std::int32_t SCE_FIOS_ERROR_FILE_NOT_FOUND     = static_cast<std::int32_t>(0x80820003);
constexpr std::int32_t SCE_FIOS_ERROR_PATH_NOT_FOUND     = static_cast<std::int32_t>(0x80820004);
constexpr std::int32_t SCE_FIOS_ERROR_NULL_POINTER        = static_cast<std::int32_t>(0x80820006);
constexpr std::int32_t SCE_FIOS_ERROR_INVALID_ARGUMENT    = static_cast<std::int32_t>(0x80820007);
constexpr std::int32_t SCE_FIOS_ERROR_INVALID_SIZE        = static_cast<std::int32_t>(0x80820008);
constexpr std::int32_t SCE_FIOS_ERROR_BAD_FH              = static_cast<std::int32_t>(0x80820009);
constexpr std::int32_t SCE_FIOS_ERROR_BAD_OP              = static_cast<std::int32_t>(0x8082000a);
constexpr std::int32_t SCE_FIOS_ERROR_EOF                 = static_cast<std::int32_t>(0x80820014);
constexpr std::int32_t SCE_FIOS_ERROR_OUT_OF_RANGE        = static_cast<std::int32_t>(0x8082001c);
constexpr std::int32_t SCE_FIOS_CALLBACK_HANDLED          = static_cast<std::int32_t>(0x8082001d);
constexpr std::int32_t SCE_FIOS_ERROR_CANNOT_WRITE        = static_cast<std::int32_t>(0x80820021);

// Handle types
using SceFiosFH = std::int32_t;
using SceFiosDH = std::int32_t;
using SceFiosOp = std::int32_t;

constexpr SceFiosFH SCE_FIOS_FH_INVALID = 0;
constexpr SceFiosDH SCE_FIOS_DH_INVALID = 0;
constexpr SceFiosOp SCE_FIOS_OP_INVALID = 0;

// Authentic SceFiosOpAttr layout
struct alignas(8) SceFiosOpAttr {
    std::uint64_t flags{0};           // offset 0x00
    void*         callback{nullptr};  // offset 0x08: void (*callback)(void* context, SceFiosOp op, std::int32_t status, std::int32_t error)
    void*         context{nullptr};   // offset 0x10: user context pointer
    std::uint32_t priority{0};        // offset 0x18
};

// Authentic Stat layout (80 bytes / 0x50)
struct alignas(8) SceFiosStat {
    std::int64_t size{0};             // offset 0x00: file size in bytes
    std::int64_t ctime_nsec{0};       // offset 0x08: creation time (ns)
    std::int64_t mtime_nsec{0};       // offset 0x10: modification time (ns)
    std::int64_t atime_nsec{0};       // offset 0x18: access time (ns)
    std::int64_t reserved20{0};       // offset 0x20
    std::int64_t reserved28{0};       // offset 0x28
    std::int64_t reserved30{0};       // offset 0x30
    std::int64_t reserved38{0};       // offset 0x38
    std::int64_t reserved40{0};       // offset 0x40
    std::int64_t mode{0};             // offset 0x48: POSIX-like mode (0100644 reg, 0040755 dir)
};
static_assert(sizeof(SceFiosStat) == 0x50, "SceFiosStat must be 80 bytes (0x50)");

// Authentic Directory Entry layout (1048 bytes / 0x418)
struct alignas(8) SceFiosDirEntry {
    SceFiosStat stat{};               // offset 0x00..0x4f: stat
    char name[968]{};                 // offset 0x50..0x417: null-terminated entry name
};
static_assert(sizeof(SceFiosDirEntry) == 0x418, "SceFiosDirEntry must be 1048 bytes (0x418)");

extern "C" {
// 1. Lifecycle
GRAVITY_SYSV_ABI std::int32_t sceFiosInitialize(const SceFiosParams* params);
GRAVITY_SYSV_ABI std::int32_t sceFiosTerminate();

// 2. IO Filter API
GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterAdd(
    std::int32_t order, void* filter_callback, void* filter_context);
GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterRemove(std::int32_t order);
GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterPsarcDearchiver(
    void* context, void* op, void* data);

// 3. Archive Mount API
GRAVITY_SYSV_ABI std::int64_t sceFiosArchiveGetMountBufferSizeSync(
    const void* attr, const char* path, const void* params);
GRAVITY_SYSV_ABI std::int32_t sceFiosArchiveMountSync(
    const void* attr, std::int32_t* out_mount_id, const char* archive_path,
    const char* mount_point, void* buffer, std::size_t buffer_size, const void* params);
GRAVITY_SYSV_ABI std::int32_t sceFiosArchiveUnmountSync(
    const void* attr, std::int32_t mount_id);

// 4. File Handle API (FH)
GRAVITY_SYSV_ABI std::int32_t sceFiosFHOpenSync(
    const void* attr, SceFiosFH* out_fh, const char* path, const void* params);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHOpen(
    const void* attr, SceFiosFH* out_fh, const char* path, const void* params);
GRAVITY_SYSV_ABI std::int64_t sceFiosFHReadSync(
    const void* attr, SceFiosFH fh, void* data, std::size_t size);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHRead(
    const void* attr, SceFiosFH fh, void* data, std::size_t size);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHPread(
    const void* attr, SceFiosFH fh, void* data, std::size_t size, std::int64_t offset);
GRAVITY_SYSV_ABI std::int32_t sceFiosFHCloseSync(
    const void* attr, SceFiosFH fh);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHClose(
    const void* attr, SceFiosFH fh);
GRAVITY_SYSV_ABI std::int64_t sceFiosFHGetSize(SceFiosFH fh);
GRAVITY_SYSV_ABI std::int64_t sceFiosFHSeek(
    SceFiosFH fh, std::int64_t offset, std::int32_t whence);
GRAVITY_SYSV_ABI std::int64_t sceFiosFHTell(SceFiosFH fh);
GRAVITY_SYSV_ABI std::int32_t sceFiosFHSyncSync(
    const void* attr, SceFiosFH fh);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHWrite(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size);
GRAVITY_SYSV_ABI std::int64_t sceFiosFHWriteSync(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFHPwrite(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size, std::int64_t offset);

// 5. Path Queries & Direct I/O
GRAVITY_SYSV_ABI std::int64_t sceFiosFileGetSizeSync(
    const void* attr, const char* path);
GRAVITY_SYSV_ABI std::int32_t sceFiosExistsSync(
    const void* attr, const char* path);
GRAVITY_SYSV_ABI std::int32_t sceFiosDirectoryExistsSync(
    const void* attr, const char* path);
GRAVITY_SYSV_ABI std::int32_t sceFiosStatSync(
    const void* attr, const char* path, SceFiosStat* out_stat);
GRAVITY_SYSV_ABI std::int64_t sceFiosFileReadSync(
    const void* attr, const char* path, void* data, std::size_t size, std::int64_t offset);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFileRead(
    const void* attr, const char* path, void* data, std::size_t size, std::int64_t offset);
GRAVITY_SYSV_ABI std::int64_t sceFiosFileWriteSync(
    const void* attr, const char* path, const void* data, std::size_t size, std::int64_t offset);
GRAVITY_SYSV_ABI SceFiosOp sceFiosFileWrite(
    const void* attr, const char* path, const void* data, std::size_t size, std::int64_t offset);
GRAVITY_SYSV_ABI std::int32_t sceFiosFileDeleteSync(
    const void* attr, const char* path);

// 6. Directory Handle API (DH)
GRAVITY_SYSV_ABI std::int32_t sceFiosDHOpenSync(
    const void* attr, SceFiosDH* out_dh, const char* path, const void* params);
GRAVITY_SYSV_ABI SceFiosOp sceFiosDHOpen(
    const void* attr, SceFiosDH* out_dh, const char* path, const void* params);
GRAVITY_SYSV_ABI std::int32_t sceFiosDHReadSync(
    const void* attr, SceFiosDH dh, SceFiosDirEntry* out_entry);
GRAVITY_SYSV_ABI std::int32_t sceFiosDHCloseSync(
    const void* attr, SceFiosDH dh);
GRAVITY_SYSV_ABI std::int32_t sceFiosDirectoryCreate(
    const void* attr, const char* path);

// 7. Operations (Op)
GRAVITY_SYSV_ABI std::int32_t sceFiosOpWait(SceFiosOp op);
GRAVITY_SYSV_ABI std::int32_t sceFiosOpGetError(SceFiosOp op);
GRAVITY_SYSV_ABI std::int64_t sceFiosOpGetActualCount(SceFiosOp op);
GRAVITY_SYSV_ABI std::int32_t sceFiosOpDelete(SceFiosOp op);

// 8. Debug / Diagnostics
GRAVITY_SYSV_ABI std::int32_t sceFiosDebugDumpError(
    std::int32_t error, char* out_buf, std::size_t buf_size);
}

[[nodiscard]] bool is_initialized() noexcept;
[[nodiscard]] const SceFiosParams& current_params() noexcept;
void reset() noexcept;

} // namespace gravity::hle::fios

