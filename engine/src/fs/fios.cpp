#include "hle/fs/fios.h"
#include "hle/fs.h"
#include "hle/guest_memory.h"
#include <cstdio>
#include <mutex>
#include <cstring>
#include <array>
#include <string>
#include <unordered_map>

namespace gravity::hle::fios {

namespace {

struct FilterSlot {
    void* callback{nullptr};
    void* context{nullptr};
};

struct MountSlot {
    bool active{false};
    std::string archive_path;
    std::string mount_point;
    void* buffer{nullptr};
    std::size_t buffer_size{0};
};

struct FiosOp {
    std::int32_t id{0};
    std::int32_t error{0};
    std::int64_t actual_count{0};
};

std::mutex g_fios_mutex;
bool g_initialized{false};
SceFiosParams g_params{};
std::array<FilterSlot, 256> g_filter_slots{};
std::unordered_map<std::int32_t, MountSlot> g_mount_slots{};
std::int32_t g_next_mount_id{1};
std::unordered_map<std::int32_t, FiosOp> g_ops{};
std::int32_t g_next_op_id{1};

} // anonymous namespace

bool is_initialized() noexcept {
    std::lock_guard lock(g_fios_mutex);
    return g_initialized;
}

const SceFiosParams& current_params() noexcept {
    std::lock_guard lock(g_fios_mutex);
    return g_params;
}

void reset() noexcept {
    std::lock_guard lock(g_fios_mutex);
    g_initialized = false;
    std::memset(&g_params, 0, sizeof(g_params));
    g_filter_slots.fill(FilterSlot{});
    g_mount_slots.clear();
    g_next_mount_id = 1;
    g_ops.clear();
    g_next_op_id = 1;
}

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceFiosInitialize(const SceFiosParams* params) {
    if (!params) {
        std::fprintf(stderr, "[FIOS] sceFiosInitialize: NULL parameters (0x80820006)\n");
        return SCE_FIOS_ERROR_NULL_POINTER;
    }

    if (!guest_memory::accessible(params, sizeof(SceFiosParams))) {
        std::fprintf(stderr, "[FIOS] sceFiosInitialize: Inaccessible memory at %p (0x80820006)\n", static_cast<const void*>(params));
        return SCE_FIOS_ERROR_NULL_POINTER;
    }

    // Authentic size extraction: bextr with control 0x0f01 -> (dword >> 1) & 0x7FFF
    const std::uint32_t extracted_size = (params->size >> 1) & 0x7FFF;
    if (extracted_size != sizeof(SceFiosParams)) {
        std::fprintf(stderr, "[FIOS] sceFiosInitialize: Unsupported paramsSize (%u, expected %zu) (0x80820008)\n",
                     extracted_size, sizeof(SceFiosParams));
        return SCE_FIOS_ERROR_INVALID_SIZE;
    }

    std::lock_guard lock(g_fios_mutex);
    if (g_initialized) {
        std::fprintf(stderr, "[FIOS] sceFiosInitialize: WARNING: FIOS has already been initialized. Updating parameters with new values.\n");
        std::memcpy(&g_params, params, sizeof(SceFiosParams));
        return SCE_FIOS_OK;
    }

    std::memcpy(&g_params, params, sizeof(SceFiosParams));
    g_initialized = true;

    std::fprintf(stderr, "[FIOS] Initialized: threads=%u, extra=%u, stack=%u, prio_io=0x%x, prio_worker=0x%x\n",
                 g_params.thread_count, g_params.extra_threads, g_params.stack_size,
                 g_params.thread_priority_io, g_params.thread_priority_worker);
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosTerminate() {
    std::lock_guard lock(g_fios_mutex);
    if (!g_initialized) {
        std::fprintf(stderr, "[FIOS] sceFiosTerminate: WARNING: FIOS is not initialized.\n");
        return SCE_FIOS_OK;
    }

    g_initialized = false;
    std::memset(&g_params, 0, sizeof(g_params));
    g_filter_slots.fill(FilterSlot{});
    g_mount_slots.clear();
    std::fprintf(stderr, "[FIOS] Terminated\n");
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterAdd(
    std::int32_t order, void* filter_callback, void* filter_context) {
    if (!filter_callback) {
        std::fprintf(stderr, "[FIOS] sceFiosIOFilterAdd: NULL callback (0x80820006)\n");
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    if (order < 0 || order >= static_cast<std::int32_t>(g_filter_slots.size())) {
        std::fprintf(stderr, "[FIOS] sceFiosIOFilterAdd: order %d out of range (0x8082001c)\n", order);
        return SCE_FIOS_ERROR_OUT_OF_RANGE;
    }

    // Call callback to initialize
    using FilterInitFn = std::int32_t(GRAVITY_SYSV_ABI*)(void* context, void* op, void* data);
    auto fn = reinterpret_cast<FilterInitFn>(filter_callback);
    const std::int32_t cb_res = fn(filter_context, nullptr, nullptr);
    if (cb_res != SCE_FIOS_OK && cb_res != SCE_FIOS_CALLBACK_HANDLED) {
        std::fprintf(stderr, "[FIOS] sceFiosIOFilterAdd: filter callback returned error 0x%x\n", cb_res);
        return cb_res;
    }

    std::lock_guard lock(g_fios_mutex);
    g_filter_slots[order] = FilterSlot{filter_callback, filter_context};
    std::fprintf(stderr, "[FIOS] IO Filter added: order=%d, callback=%p, context=%p\n",
                 order, filter_callback, filter_context);
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterRemove(std::int32_t order) {
    if (order < 0 || order >= static_cast<std::int32_t>(g_filter_slots.size())) {
        return SCE_FIOS_ERROR_OUT_OF_RANGE;
    }
    std::lock_guard lock(g_fios_mutex);
    g_filter_slots[order] = FilterSlot{};
    std::fprintf(stderr, "[FIOS] IO Filter removed: order=%d\n", order);
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosIOFilterPsarcDearchiver(
    void* context, void* op, void* data) {
    (void)data;
    if (!op) {
        // Initialization call: validate context if provided
        if (context) {
            std::uint64_t ctx_size = 0;
            if (guest_memory::read(context, &ctx_size, sizeof(ctx_size))) {
                if (ctx_size != 0x38) {
                    std::fprintf(stderr, "[FIOS] sceFiosIOFilterPsarcDearchiver: sizeOfContext %llu != 56\n",
                                 static_cast<unsigned long long>(ctx_size));
                }
            }
        }
        return SCE_FIOS_OK;
    }
    // Decompression / read operation call
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosArchiveGetMountBufferSizeSync(
    const void* attr, const char* path, const void* params) {
    (void)attr; (void)params;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    std::fprintf(stderr, "[FIOS] ArchiveGetMountBufferSizeSync: %s\n", path_buf);
    // Standard mount buffer size required by PSARC decompressor
    return 0x20000;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosArchiveMountSync(
    const void* attr, const char* archive_path, const char* mount_point,
    void* buffer, std::size_t buffer_size, const void* params, std::int32_t* out_mount_id) {
    (void)attr; (void)params;
    if (!archive_path || !mount_point) return SCE_FIOS_ERROR_NULL_POINTER;

    char arc_buf[260]{};
    char mnt_buf[260]{};
    if (!guest_memory::string(archive_path, arc_buf, sizeof(arc_buf)) ||
        !guest_memory::string(mount_point, mnt_buf, sizeof(mnt_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }

    std::lock_guard lock(g_fios_mutex);
    const std::int32_t mount_id = g_next_mount_id++;
    g_mount_slots[mount_id] = MountSlot{true, arc_buf, mnt_buf, buffer, buffer_size};

    if (out_mount_id) {
        guest_memory::write(out_mount_id, &mount_id, sizeof(mount_id));
    }

    std::fprintf(stderr, "[FIOS] ArchiveMountSync: archive=%s, mount_point=%s, id=%d\n",
                 arc_buf, mnt_buf, mount_id);
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosArchiveUnmountSync(
    const void* attr, std::int32_t mount_id) {
    (void)attr;
    std::lock_guard lock(g_fios_mutex);
    auto it = g_mount_slots.find(mount_id);
    if (it != g_mount_slots.end()) {
        std::fprintf(stderr, "[FIOS] ArchiveUnmountSync: id=%d\n", mount_id);
        g_mount_slots.erase(it);
    }
    return SCE_FIOS_OK;
}

// ---------------------------------------------------------------------
// 4. File Handle API (FH)
// ---------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int32_t sceFiosFHOpenSync(
    const void* attr, SceFiosFH* out_fh, const char* path, const void* params) {
    (void)attr; (void)params;
    if (!path || !out_fh) return SCE_FIOS_ERROR_NULL_POINTER;
    if (!guest_memory::accessible(out_fh, sizeof(SceFiosFH))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    const std::int32_t fd = fs::vfs_open(path_buf, 0, 0);
    if (fd < 0) {
        std::fprintf(stderr, "[FIOS] sceFiosFHOpenSync: Failed to open '%s'\n", path_buf);
        return SCE_FIOS_ERROR_FILE_NOT_FOUND;
    }
    guest_memory::write(out_fh, &fd, sizeof(fd));
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHOpen(
    const void* attr, SceFiosFH* out_fh, const char* path, const void* params) {
    (void)attr; (void)params;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    if (!path || !out_fh || !guest_memory::accessible(out_fh, sizeof(SceFiosFH))) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    const std::int32_t fd = fs::vfs_open(path_buf, 0, 0);
    if (fd < 0) {
        std::fprintf(stderr, "[FIOS] sceFiosFHOpen: Failed to open '%s'\n", path_buf);
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_FILE_NOT_FOUND, 0};
        return op;
    }
    guest_memory::write(out_fh, &fd, sizeof(fd));
    g_ops[op] = FiosOp{op, SCE_FIOS_OK, 0};
    return op;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFHReadSync(
    const void* attr, SceFiosFH fh, void* data, std::size_t size) {
    (void)attr;
    if (size == 0) return 0;
    if (!data || !guest_memory::accessible(data, size)) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    const std::int32_t read_bytes = fs::vfs_read(fh, data, static_cast<std::uint32_t>(size));
    if (read_bytes < 0) return SCE_FIOS_ERROR_BAD_FH;
    return read_bytes;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHRead(
    const void* attr, SceFiosFH fh, void* data, std::size_t size) {
    (void)attr;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    if (size == 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, 0};
        return op;
    }
    if (!data || !guest_memory::accessible(data, size)) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    const std::int32_t read_bytes = fs::vfs_read(fh, data, static_cast<std::uint32_t>(size));
    if (read_bytes < 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_BAD_FH, 0};
    } else {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, read_bytes};
    }
    return op;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHPread(
    const void* attr, SceFiosFH fh, void* data, std::size_t size, std::int64_t offset) {
    (void)attr;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    if (size == 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, 0};
        return op;
    }
    if (!data || !guest_memory::accessible(data, size)) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    const std::int32_t read_bytes = fs::vfs_pread(fh, data, static_cast<std::uint32_t>(size), offset);
    if (read_bytes < 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_BAD_FH, 0};
    } else {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, read_bytes};
    }
    return op;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosFHCloseSync(const void* attr, SceFiosFH fh) {
    (void)attr;
    if (fs::vfs_close(fh) < 0) return SCE_FIOS_ERROR_BAD_FH;
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHClose(const void* attr, SceFiosFH fh) {
    (void)attr;
    const std::int32_t res = fs::vfs_close(fh);
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    g_ops[op] = FiosOp{op, res < 0 ? SCE_FIOS_ERROR_BAD_FH : SCE_FIOS_OK, 0};
    return op;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFHGetSize(SceFiosFH fh) {
    const std::int64_t sz = fs::vfs_get_size(fh);
    if (sz < 0) return SCE_FIOS_ERROR_BAD_FH;
    return sz;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFHSeek(
    SceFiosFH fh, std::int64_t offset, std::int32_t whence) {
    const std::int64_t new_pos = fs::vfs_lseek(fh, offset, whence);
    if (new_pos < 0) return SCE_FIOS_ERROR_BAD_FH;
    return new_pos;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFHTell(SceFiosFH fh) {
    const std::int64_t pos = fs::vfs_tell(fh);
    if (pos < 0) return SCE_FIOS_ERROR_BAD_FH;
    return pos;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosFHSyncSync(const void* attr, SceFiosFH fh) {
    (void)attr; (void)fh;
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHWrite(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size) {
    (void)attr; (void)fh; (void)data; (void)size;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_CANNOT_WRITE, 0};
    return op;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFHWriteSync(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size) {
    (void)attr; (void)fh; (void)data; (void)size;
    return SCE_FIOS_ERROR_CANNOT_WRITE;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFHPwrite(
    const void* attr, SceFiosFH fh, const void* data, std::size_t size, std::int64_t offset) {
    (void)attr; (void)fh; (void)data; (void)size; (void)offset;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_CANNOT_WRITE, 0};
    return op;
}

// ---------------------------------------------------------------------
// 5. Path Queries & Direct I/O
// ---------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int64_t sceFiosFileGetSizeSync(
    const void* attr, const char* path) {
    (void)attr;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    const std::int64_t sz = fs::vfs_file_size(path_buf);
    if (sz < 0) return SCE_FIOS_ERROR_FILE_NOT_FOUND;
    return sz;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosExistsSync(
    const void* attr, const char* path) {
    (void)attr;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    return fs::vfs_exists(path_buf) ? 1 : 0;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosDirectoryExistsSync(
    const void* attr, const char* path) {
    (void)attr;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    return fs::vfs_is_dir(path_buf) ? 1 : 0;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosStatSync(
    const void* attr, const char* path, SceFiosStat* out_stat) {
    (void)attr;
    if (!path || !out_stat) return SCE_FIOS_ERROR_NULL_POINTER;
    if (!guest_memory::accessible(out_stat, sizeof(SceFiosStat))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    SceFiosStat st{};
    const std::int32_t res = fs::vfs_stat(path_buf, &st);
    if (res != 0) return SCE_FIOS_ERROR_FILE_NOT_FOUND;
    guest_memory::write(out_stat, &st, sizeof(st));
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFileReadSync(
    const void* attr, const char* path, std::int64_t offset, void* data, std::size_t size) {
    (void)attr;
    if (size == 0) return 0;
    if (!path || !data) return SCE_FIOS_ERROR_NULL_POINTER;
    if (!guest_memory::accessible(data, size)) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    const std::int32_t fd = fs::vfs_open(path_buf, 0, 0);
    if (fd < 0) return SCE_FIOS_ERROR_FILE_NOT_FOUND;
    const std::int32_t read_bytes = fs::vfs_pread(fd, data, static_cast<std::uint32_t>(size), offset);
    fs::vfs_close(fd);
    if (read_bytes < 0) return SCE_FIOS_ERROR_BAD_FH;
    return read_bytes;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFileRead(
    const void* attr, const char* path, std::int64_t offset, void* data, std::size_t size) {
    (void)attr;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    if (size == 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, 0};
        return op;
    }
    if (!path || !data || !guest_memory::accessible(data, size)) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    const std::int32_t fd = fs::vfs_open(path_buf, 0, 0);
    if (fd < 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_FILE_NOT_FOUND, 0};
        return op;
    }
    const std::int32_t read_bytes = fs::vfs_pread(fd, data, static_cast<std::uint32_t>(size), offset);
    fs::vfs_close(fd);
    if (read_bytes < 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_BAD_FH, 0};
    } else {
        g_ops[op] = FiosOp{op, SCE_FIOS_OK, read_bytes};
    }
    return op;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosFileWriteSync(
    const void* attr, const char* path, std::int64_t offset, const void* data, std::size_t size) {
    (void)attr; (void)path; (void)offset; (void)data; (void)size;
    return SCE_FIOS_ERROR_CANNOT_WRITE;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosFileWrite(
    const void* attr, const char* path, std::int64_t offset, const void* data, std::size_t size) {
    (void)attr; (void)path; (void)offset; (void)data; (void)size;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_CANNOT_WRITE, 0};
    return op;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosFileDeleteSync(
    const void* attr, const char* path) {
    (void)attr;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    return SCE_FIOS_OK;
}

// ---------------------------------------------------------------------
// 6. Directory Handle API (DH)
// ---------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int32_t sceFiosDHOpenSync(
    const void* attr, SceFiosDH* out_dh, const char* path, const void* params) {
    (void)attr; (void)params;
    if (!path || !out_dh) return SCE_FIOS_ERROR_NULL_POINTER;
    if (!guest_memory::accessible(out_dh, sizeof(SceFiosDH))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    const std::int32_t dh = fs::vfs_opendir(path_buf);
    if (dh < 0) return SCE_FIOS_ERROR_PATH_NOT_FOUND;
    guest_memory::write(out_dh, &dh, sizeof(dh));
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI SceFiosOp sceFiosDHOpen(
    const void* attr, SceFiosDH* out_dh, const char* path, const void* params) {
    (void)attr; (void)params;
    std::lock_guard lock(g_fios_mutex);
    const SceFiosOp op = g_next_op_id++;
    if (!path || !out_dh || !guest_memory::accessible(out_dh, sizeof(SceFiosDH))) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_NULL_POINTER, 0};
        return op;
    }
    const std::int32_t dh = fs::vfs_opendir(path_buf);
    if (dh < 0) {
        g_ops[op] = FiosOp{op, SCE_FIOS_ERROR_PATH_NOT_FOUND, 0};
        return op;
    }
    guest_memory::write(out_dh, &dh, sizeof(dh));
    g_ops[op] = FiosOp{op, SCE_FIOS_OK, 0};
    return op;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosDHReadSync(
    const void* attr, SceFiosDH dh, SceFiosDirEntry* out_entry) {
    (void)attr;
    if (!out_entry || !guest_memory::accessible(out_entry, sizeof(SceFiosDirEntry))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    SceFiosDirEntry entry{};
    const std::int32_t res = fs::vfs_readdir(dh, &entry);
    if (res < 0) return SCE_FIOS_ERROR_EOF;
    guest_memory::write(out_entry, &entry, sizeof(entry));
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosDHCloseSync(const void* attr, SceFiosDH dh) {
    (void)attr;
    if (fs::vfs_closedir(dh) < 0) return SCE_FIOS_ERROR_BAD_FH;
    return SCE_FIOS_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosDirectoryCreate(
    const void* attr, const char* path) {
    (void)attr;
    if (!path) return SCE_FIOS_ERROR_NULL_POINTER;
    char path_buf[260]{};
    if (!guest_memory::string(path, path_buf, sizeof(path_buf))) {
        return SCE_FIOS_ERROR_NULL_POINTER;
    }
    return SCE_FIOS_OK;
}

// ---------------------------------------------------------------------
// 7. Operations (Op)
// ---------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int32_t sceFiosOpWait(SceFiosOp op) {
    std::lock_guard lock(g_fios_mutex);
    const auto it = g_ops.find(op);
    if (it == g_ops.end()) return SCE_FIOS_ERROR_BAD_OP;
    return it->second.error;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosOpGetError(SceFiosOp op) {
    std::lock_guard lock(g_fios_mutex);
    const auto it = g_ops.find(op);
    if (it == g_ops.end()) return SCE_FIOS_ERROR_BAD_OP;
    return it->second.error;
}

GRAVITY_SYSV_ABI std::int64_t sceFiosOpGetActualCount(SceFiosOp op) {
    std::lock_guard lock(g_fios_mutex);
    const auto it = g_ops.find(op);
    if (it == g_ops.end()) return 0;
    return it->second.actual_count;
}

GRAVITY_SYSV_ABI std::int32_t sceFiosOpDelete(SceFiosOp op) {
    std::lock_guard lock(g_fios_mutex);
    g_ops.erase(op);
    return SCE_FIOS_OK;
}

// ---------------------------------------------------------------------
// 8. Debug / Diagnostics
// ---------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int32_t sceFiosDebugDumpError(
    std::int32_t error, char* out_buf, std::size_t buf_size) {
    const char* desc = "Unknown FIOS error";
    switch (error) {
    case SCE_FIOS_OK: desc = "SCE_FIOS_OK"; break;
    case SCE_FIOS_ERROR_ALREADY_INITIALIZED: desc = "SCE_FIOS_ERROR_ALREADY_INITIALIZED"; break;
    case SCE_FIOS_ERROR_NOT_INITIALIZED: desc = "SCE_FIOS_ERROR_NOT_INITIALIZED"; break;
    case SCE_FIOS_ERROR_FILE_NOT_FOUND: desc = "SCE_FIOS_ERROR_FILE_NOT_FOUND"; break;
    case SCE_FIOS_ERROR_PATH_NOT_FOUND: desc = "SCE_FIOS_ERROR_PATH_NOT_FOUND"; break;
    case SCE_FIOS_ERROR_NULL_POINTER: desc = "SCE_FIOS_ERROR_NULL_POINTER"; break;
    case SCE_FIOS_ERROR_INVALID_ARGUMENT: desc = "SCE_FIOS_ERROR_INVALID_ARGUMENT"; break;
    case SCE_FIOS_ERROR_INVALID_SIZE: desc = "SCE_FIOS_ERROR_INVALID_SIZE"; break;
    case SCE_FIOS_ERROR_BAD_FH: desc = "SCE_FIOS_ERROR_BAD_FH"; break;
    case SCE_FIOS_ERROR_BAD_OP: desc = "SCE_FIOS_ERROR_BAD_OP"; break;
    case SCE_FIOS_ERROR_EOF: desc = "SCE_FIOS_ERROR_EOF"; break;
    case SCE_FIOS_ERROR_OUT_OF_RANGE: desc = "SCE_FIOS_ERROR_OUT_OF_RANGE"; break;
    case SCE_FIOS_CALLBACK_HANDLED: desc = "SCE_FIOS_CALLBACK_HANDLED"; break;
    case SCE_FIOS_ERROR_CANNOT_WRITE: desc = "SCE_FIOS_ERROR_CANNOT_WRITE"; break;
    default: break;
    }
    if (out_buf && buf_size > 0) {
        if (guest_memory::accessible(out_buf, buf_size)) {
            std::snprintf(out_buf, buf_size, "%s (0x%x)", desc, error);
        }
    }
    std::fprintf(stderr, "[FIOS] sceFiosDebugDumpError: 0x%x (%s)\n", error, desc);
    return SCE_FIOS_OK;
}

} // extern "C"

} // namespace gravity::hle::fios
