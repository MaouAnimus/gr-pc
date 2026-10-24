#include "hle/fs/fios.h"
#include "hle/fs.h"
#include "hle/registry/adapter_registry.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::fios;
using InitFn = std::int32_t(GRAVITY_SYSV_ABI*)(const SceFiosParams*);
using TermFn = std::int32_t(GRAVITY_SYSV_ABI*)();
using FilterAddFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, void*, void*);
using FilterRemoveFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t);
using MountBufferFn = std::int64_t(GRAVITY_SYSV_ABI*)(const void*, const char*, const void*);
using MountFn = std::int32_t(GRAVITY_SYSV_ABI*)(const void*, const char*, const char*, void*, std::size_t, const void*, std::int32_t*);
using UnmountFn = std::int32_t(GRAVITY_SYSV_ABI*)(const void*, std::int32_t);

static GRAVITY_SYSV_ABI std::int32_t dummy_filter_cb(void* context, void* op, void* data) {
    (void)context; (void)op; (void)data;
    return SCE_FIOS_OK;
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];

    auto& registry = gravity::hle::AdapterRegistry::instance();
    const auto fn_init = reinterpret_cast<InitFn>(registry.resolve("wAKZ-det+yo", "libSceFios2"));
    const auto fn_term = reinterpret_cast<TermFn>(registry.resolve("3HAgZPl1v+4", "libSceFios2"));
    const auto fn_filter_add = reinterpret_cast<FilterAddFn>(registry.resolve("lgITuBsRo2o", "libSceFios2"));
    const auto fn_filter_remove = reinterpret_cast<FilterRemoveFn>(registry.resolve("ahIXyuwF0-o", "libSceFios2"));
    const auto fn_mount_buffer = reinterpret_cast<MountBufferFn>(registry.resolve("UUriaXy7G90", "libSceFios2"));
    const auto fn_mount = reinterpret_cast<MountFn>(registry.resolve("xutLbQdqyb4", "libSceFios2"));
    const auto fn_unmount = reinterpret_cast<UnmountFn>(registry.resolve("yy6C7k7FPZY", "libSceFios2"));

    CHECK(fn_init == &sceFiosInitialize);
    CHECK(fn_term == &sceFiosTerminate);
    CHECK(fn_filter_add == &sceFiosIOFilterAdd);
    CHECK(fn_filter_remove == &sceFiosIOFilterRemove);
    CHECK(fn_mount_buffer == &sceFiosArchiveGetMountBufferSizeSync);
    CHECK(fn_mount == &sceFiosArchiveMountSync);
    CHECK(fn_unmount == &sceFiosArchiveUnmountSync);

    if (suite == "registry") {
        CHECK(registry.resolve("wAKZ-det+yo", "libSceVideoOut") == 0);
        CHECK(registry.resolve("3HAgZPl1v+4", "libkernel") == 0);

        const auto* sym_init = registry.find("wAKZ-det+yo");
        CHECK(sym_init && sym_init->name == "sceFiosInitialize" && !sym_init->is_variable);
        CHECK(sym_init->library == "libSceFios2");

        const auto* sym_term = registry.find("3HAgZPl1v+4");
        CHECK(sym_term && sym_term->name == "sceFiosTerminate" && !sym_term->is_variable);
        CHECK(sym_term->library == "libSceFios2");

        const auto* sym_filter_add = registry.find("lgITuBsRo2o");
        CHECK(sym_filter_add && sym_filter_add->name == "sceFiosIOFilterAdd");

        const auto* sym_filter_remove = registry.find("ahIXyuwF0-o");
        CHECK(sym_filter_remove && sym_filter_remove->name == "sceFiosIOFilterRemove");

        const auto* sym_psarc = registry.find("OIGbkgGOu6E");
        CHECK(sym_psarc && sym_psarc->name == "sceFiosIOFilterPsarcDearchiver");

        const auto* sym_memalign = registry.find("Ujf3KzMvRmI");
        CHECK(sym_memalign && sym_memalign->name == "memalign" && sym_memalign->library == "libc");

        const auto* sym_attr_destroy = registry.find("62KCwEMmzcM");
        CHECK(sym_attr_destroy && sym_attr_destroy->name == "scePthreadAttrDestroy" && !sym_attr_destroy->is_variable);
        CHECK(sym_attr_destroy->library == "libkernel");

        // Verify newly registered FIOS symbols
        const auto* sym_fh_open_sync = registry.find("b44anV2D7K0");
        CHECK(sym_fh_open_sync && sym_fh_open_sync->name == "sceFiosFHOpenSync");
        CHECK(sym_fh_open_sync->library == "libSceFios2");

        const auto* sym_fh_open = registry.find("er6TkQFUvp0");
        CHECK(sym_fh_open && sym_fh_open->name == "sceFiosFHOpen");

        const auto* sym_fh_read_sync = registry.find("Bn2ZF4ZjeuQ");
        CHECK(sym_fh_read_sync && sym_fh_read_sync->name == "sceFiosFHReadSync");

        const auto* sym_fh_close_sync = registry.find("AOujSGqU+ms");
        CHECK(sym_fh_close_sync && sym_fh_close_sync->name == "sceFiosFHCloseSync");

        const auto* sym_fh_get_size = registry.find("FdjoqFQOlt0");
        CHECK(sym_fh_get_size && sym_fh_get_size->name == "sceFiosFHGetSize");

        const auto* sym_file_exists = registry.find("2ZaHWy3IhKQ");
        CHECK(sym_file_exists && sym_file_exists->name == "sceFiosExistsSync");

        const auto* sym_dir_exists = registry.find("OOuvHKTu4Oc");
        CHECK(sym_dir_exists && sym_dir_exists->name == "sceFiosDirectoryExistsSync");

        const auto* sym_op_wait = registry.find("SnoQQWnGK9I");
        CHECK(sym_op_wait && sym_op_wait->name == "sceFiosOpWait");

        const auto* sym_op_error = registry.find("X+7rIfY97Ps");
        CHECK(sym_op_error && sym_op_error->name == "sceFiosOpGetError");

        const auto* sym_op_count = registry.find("+FRvKknUj1I");
        CHECK(sym_op_count && sym_op_count->name == "sceFiosOpGetActualCount");

        const auto* sym_op_delete = registry.find("5cyEcilO-J0");
        CHECK(sym_op_delete && sym_op_delete->name == "sceFiosOpDelete");
    } else if (suite == "lifecycle") {
        reset();
        CHECK(!is_initialized());

        SceFiosParams params{};
        params.size = 0x1000130; // (0x98 << 1) | 0x1000000
        params.thread_count = 2;
        params.extra_threads = 1;
        params.stack_size = 0x80000;
        params.thread_priority_io = 0x2ae;
        params.thread_priority_worker = 0x2b0;
        params.buffer30 = reinterpret_cast<void*>(0x1d80e60);
        params.size38 = 0x3f040;

        CHECK(fn_init(&params) == SCE_FIOS_OK);
        CHECK(is_initialized());

        const auto& cur = current_params();
        CHECK(cur.size == 0x1000130);
        CHECK(cur.thread_count == 2);
        CHECK(cur.extra_threads == 1);
        CHECK(cur.stack_size == 0x80000);
        CHECK(cur.thread_priority_io == 0x2ae);
        CHECK(cur.thread_priority_worker == 0x2b0);
        CHECK(cur.buffer30 == reinterpret_cast<void*>(0x1d80e60));
        CHECK(cur.size38 == 0x3f040);

        // Re-initialize when already initialized: should update params and return OK
        params.thread_count = 4;
        CHECK(fn_init(&params) == SCE_FIOS_OK);
        CHECK(is_initialized());
        CHECK(current_params().thread_count == 4);

        // Terminate
        CHECK(fn_term() == SCE_FIOS_OK);
        CHECK(!is_initialized());

        // Second terminate when already uninitialized
        CHECK(fn_term() == SCE_FIOS_OK);
        CHECK(!is_initialized());
    } else if (suite == "filters") {
        reset();

        // Null callback
        CHECK(fn_filter_add(0, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);

        // Order out of range
        CHECK(fn_filter_add(-1, reinterpret_cast<void*>(&dummy_filter_cb), nullptr) == SCE_FIOS_ERROR_OUT_OF_RANGE);
        CHECK(fn_filter_add(256, reinterpret_cast<void*>(&dummy_filter_cb), nullptr) == SCE_FIOS_ERROR_OUT_OF_RANGE);

        // Valid filter add
        CHECK(fn_filter_add(0, reinterpret_cast<void*>(&dummy_filter_cb), nullptr) == SCE_FIOS_OK);

        // With sceFiosIOFilterPsarcDearchiver
        struct PsarcContext {
            std::uint64_t size{0x38};
            std::uint8_t extra[48]{};
        } ctx;
        CHECK(fn_filter_add(1, reinterpret_cast<void*>(&sceFiosIOFilterPsarcDearchiver), &ctx) == SCE_FIOS_OK);

        // Filter remove
        CHECK(fn_filter_remove(0) == SCE_FIOS_OK);
        CHECK(fn_filter_remove(1) == SCE_FIOS_OK);
        CHECK(fn_filter_remove(-1) == SCE_FIOS_ERROR_OUT_OF_RANGE);
        CHECK(fn_filter_remove(256) == SCE_FIOS_ERROR_OUT_OF_RANGE);
    } else if (suite == "mount") {
        reset();

        // Buffer size query
        CHECK(fn_mount_buffer(nullptr, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        const std::int64_t buf_size = fn_mount_buffer(nullptr, "/app0/arc/arc_0_ep_0_0.psarc", nullptr);
        CHECK(buf_size > 0);

        // Mounting
        CHECK(fn_mount(nullptr, nullptr, "/arc/", nullptr, 0, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(fn_mount(nullptr, "/app0/arc/test.psarc", nullptr, nullptr, 0, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);

        std::int32_t mount_id = 0;
        char buffer[1024]{};
        CHECK(fn_mount(nullptr, "/app0/arc/arc_0_ep_0_0.psarc", "/arc/", buffer, sizeof(buffer), nullptr, &mount_id) == SCE_FIOS_OK);
        CHECK(mount_id > 0);

        // Unmount
        CHECK(fn_unmount(nullptr, mount_id) == SCE_FIOS_OK);
        // Unmounting non-existent mount ID returns OK
        CHECK(fn_unmount(nullptr, 9999) == SCE_FIOS_OK);
    } else if (suite == "invalid") {
        reset();
        CHECK(!is_initialized());

        // Null pointer
        CHECK(fn_init(nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(!is_initialized());

        // Inaccessible pointers
        CHECK(fn_init(reinterpret_cast<const SceFiosParams*>(1)) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(fn_init(reinterpret_cast<const SceFiosParams*>(UINTPTR_MAX - 1)) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(!is_initialized());

        // Size check: ((size >> 1) & 0x7FFF) must equal 0x98 (152)
        SceFiosParams bad_params{};
        bad_params.size = 0; // extracted = 0
        CHECK(fn_init(&bad_params) == SCE_FIOS_ERROR_INVALID_SIZE);
        CHECK(!is_initialized());

        bad_params.size = (100 << 1); // extracted = 100 != 152
        CHECK(fn_init(&bad_params) == SCE_FIOS_ERROR_INVALID_SIZE);
        CHECK(!is_initialized());

        bad_params.size = (0x80 << 1); // extracted = 128 != 152
        CHECK(fn_init(&bad_params) == SCE_FIOS_ERROR_INVALID_SIZE);
        CHECK(!is_initialized());

        bad_params.size = (0x100 << 1); // extracted = 256 != 152
        CHECK(fn_init(&bad_params) == SCE_FIOS_ERROR_INVALID_SIZE);
        CHECK(!is_initialized());
    } else if (suite == "file_ops") {
        gravity::hle::fs::initialize_vfs();

        // 1. Exists checks
        CHECK(sceFiosExistsSync(nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosExistsSync(nullptr, "/app0/arc/arc_0_ep_0_0.psarc") == 1);
        CHECK(sceFiosExistsSync(nullptr, "/app0/nonexistent_file.bin") == 0);

        // 2. Directory exists checks
        CHECK(sceFiosDirectoryExistsSync(nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosDirectoryExistsSync(nullptr, "/app0/arc") == 1);
        CHECK(sceFiosDirectoryExistsSync(nullptr, "/app0/nonexistent_dir") == 0);

        // 3. File size query
        CHECK(sceFiosFileGetSizeSync(nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosFileGetSizeSync(nullptr, "/app0/nonexistent_file.bin") < 0);
        const std::int64_t fsize = sceFiosFileGetSizeSync(nullptr, "/app0/arc/arc_0_ep_0_0.psarc");
        CHECK(fsize > 0);

        // 4. Stat query
        CHECK(sceFiosStatSync(nullptr, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        SceFiosStat stat{};
        CHECK(sceFiosStatSync(nullptr, "/app0/arc/arc_0_ep_0_0.psarc", &stat) == SCE_FIOS_OK);
        CHECK(stat.size == fsize);
        CHECK((stat.mode & 0170000) == 0100000);

        // 5. Synchronous file handle operations
        SceFiosFH fh = SCE_FIOS_FH_INVALID;
        CHECK(sceFiosFHOpenSync(nullptr, nullptr, "/app0/arc/arc_0_ep_0_0.psarc", nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosFHOpenSync(nullptr, &fh, nullptr, nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosFHOpenSync(nullptr, &fh, "/app0/arc/arc_0_ep_0_0.psarc", nullptr) == SCE_FIOS_OK);
        CHECK(fh > 0);
        CHECK(sceFiosFHGetSize(fh) == fsize);

        char magic[4] = {};
        CHECK(sceFiosFHReadSync(nullptr, fh, magic, sizeof(magic)) == sizeof(magic));
        CHECK(std::memcmp(magic, "PSAR", 4) == 0);
        CHECK(sceFiosFHTell(fh) == 4);

        CHECK(sceFiosFHSeek(fh, 0, 0) == 0);
        CHECK(sceFiosFHTell(fh) == 0);
        CHECK(sceFiosFHCloseSync(nullptr, fh) == SCE_FIOS_OK);
        CHECK(sceFiosFHCloseSync(nullptr, fh) == SCE_FIOS_ERROR_BAD_FH);

        // 6. Asynchronous file handle operations
        SceFiosFH async_fh = SCE_FIOS_FH_INVALID;
        SceFiosOp open_op = sceFiosFHOpen(nullptr, &async_fh, "/app0/arc/arc_0_ep_0_0.psarc", nullptr);
        CHECK(open_op > 0);
        CHECK(sceFiosOpWait(open_op) == SCE_FIOS_OK);
        CHECK(sceFiosOpGetError(open_op) == SCE_FIOS_OK);
        CHECK(sceFiosOpDelete(open_op) == SCE_FIOS_OK);
        CHECK(async_fh > 0);

        char async_buf[16] = {};
        SceFiosOp read_op = sceFiosFHRead(nullptr, async_fh, async_buf, sizeof(async_buf));
        CHECK(read_op > 0);
        CHECK(sceFiosOpWait(read_op) == SCE_FIOS_OK);
        CHECK(sceFiosOpGetError(read_op) == SCE_FIOS_OK);
        CHECK(sceFiosOpGetActualCount(read_op) == sizeof(async_buf));
        CHECK(std::memcmp(async_buf, "PSAR", 4) == 0);
        CHECK(sceFiosOpDelete(read_op) == SCE_FIOS_OK);

        SceFiosOp close_op = sceFiosFHClose(nullptr, async_fh);
        CHECK(close_op > 0);
        CHECK(sceFiosOpWait(close_op) == SCE_FIOS_OK);
        CHECK(sceFiosOpDelete(close_op) == SCE_FIOS_OK);

        // 7. Direct file read
        char direct_buf[8] = {};
        CHECK(sceFiosFileReadSync(nullptr, "/app0/arc/arc_0_ep_0_0.psarc", 0, direct_buf, sizeof(direct_buf)) == sizeof(direct_buf));
        CHECK(std::memcmp(direct_buf, "PSAR", 4) == 0);

        // 8. Directory handle operations
        SceFiosDH dh = SCE_FIOS_DH_INVALID;
        CHECK(sceFiosDHOpenSync(nullptr, nullptr, "/app0/arc", nullptr) == SCE_FIOS_ERROR_NULL_POINTER);
        CHECK(sceFiosDHOpenSync(nullptr, &dh, "/app0/arc", nullptr) == SCE_FIOS_OK);
        CHECK(dh > 0);

        SceFiosDirEntry entry{};
        CHECK(sceFiosDHReadSync(nullptr, dh, &entry) == SCE_FIOS_OK);
        CHECK(entry.name[0] != '\0');
        CHECK(sceFiosDHCloseSync(nullptr, dh) == SCE_FIOS_OK);
        CHECK(sceFiosDHCloseSync(nullptr, dh) == SCE_FIOS_ERROR_BAD_FH);

        // 9. Debug error dump
        char err_buf[128] = {};
        CHECK(sceFiosDebugDumpError(SCE_FIOS_ERROR_FILE_NOT_FOUND, err_buf, sizeof(err_buf)) == SCE_FIOS_OK);
        CHECK(std::strstr(err_buf, "SCE_FIOS_ERROR_FILE_NOT_FOUND") != nullptr);
    } else {
        std::fprintf(stderr, "Unknown test suite: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
