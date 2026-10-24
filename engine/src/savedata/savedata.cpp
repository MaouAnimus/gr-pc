#include "hle/savedata/savedata.h"
#include "hle/guest_memory.h"
#include "hle/fs.h"
#include <filesystem>
#include <cstring>
#include <string>
#include <mutex>
#include <algorithm>
#include <fstream>

namespace gravity::hle::savedata {

namespace {

[[maybe_unused]] constexpr std::int32_t SCE_OK = 0;
[[maybe_unused]] constexpr std::int32_t SCE_SAVE_DATA_ERROR_NOT_INITIALIZED = -2140667903; // 0x80540001
[[maybe_unused]] constexpr std::int32_t SCE_SAVE_DATA_ERROR_ALREADY_INITIALIZED = -2140667902;
[[maybe_unused]] constexpr std::int32_t SCE_SAVE_DATA_ERROR_PARAMETER = -2140667901;
[[maybe_unused]] constexpr std::int32_t SCE_SAVE_DATA_ERROR_NOT_MOUNTED = -2140667899;

std::filesystem::path get_default_savedata_root() {
    const char* env_root = std::getenv("GRAVITY_SAVEDATA");
    if (env_root && *env_root) {
        return std::filesystem::path(env_root);
    }
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::filesystem::path(appdata) / "GravityRush" / "SaveData" / "CUSA01130";
    }
    return std::filesystem::path("savedata/CUSA01130");
}

struct SaveDataState {
    std::mutex mutex;
    bool user_service_initialized{false};
    bool save_data_initialized{false};
    bool dialog_initialized{false};
    bool is_mounted{false};
    std::string mount_dir_name{};
    std::filesystem::path host_save_root{get_default_savedata_root()};
    std::vector<std::uint8_t> memory_save_buffer{};
};

SaveDataState& state() {
    static SaveDataState s;
    return s;
}

} // anonymous namespace

void set_savedata_root(const char* root_path) {
    if (!root_path) return;
    std::lock_guard<std::mutex> lock(state().mutex);
    state().host_save_root = std::filesystem::path(root_path);
}

const char* get_savedata_root() {
    std::lock_guard<std::mutex> lock(state().mutex);
    static std::string path_str;
    path_str = state().host_save_root.string();
    return path_str.c_str();
}

bool is_savedata_mounted() {
    std::lock_guard<std::mutex> lock(state().mutex);
    return state().is_mounted;
}

const char* get_current_mount_dir() {
    std::lock_guard<std::mutex> lock(state().mutex);
    static std::string dir_str;
    dir_str = state().mount_dir_name;
    return dir_str.c_str();
}

extern "C" {

// =======================================================================
// libSceUserService
// =======================================================================

GRAVITY_SYSV_ABI std::int32_t sceUserServiceInitialize(const SceUserServiceInitializeParams* params) {
    (void)params;
    std::lock_guard<std::mutex> lock(state().mutex);
    state().user_service_initialized = true;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceUserServiceTerminate() {
    std::lock_guard<std::mutex> lock(state().mutex);
    state().user_service_initialized = false;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceUserServiceGetInitialUser(SceUserServiceUserId* userId) {
    if (!userId || !guest_memory::accessible(userId, sizeof(*userId))) {
        return -1;
    }
    *userId = SCE_USER_SERVICE_PRIMARY_USER_ID;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceUserServiceGetUserName(SceUserServiceUserId userId, char* userName, std::size_t size) {
    if (!userName || size == 0 || !guest_memory::accessible(userName, size)) {
        return -1;
    }
    (void)userId;
    constexpr const char default_name[] = "Player";
    std::size_t copy_len = (std::min)(size - 1, sizeof(default_name) - 1);
    std::memcpy(userName, default_name, copy_len);
    userName[copy_len] = '\0';
    return SCE_OK;
}

// =======================================================================
// libSceSaveData
// =======================================================================

GRAVITY_SYSV_ABI std::int32_t sceSaveDataInitialize(const SceSaveDataInitParams* params) {
    (void)params;
    std::lock_guard<std::mutex> lock(state().mutex);
    state().save_data_initialized = true;
    std::error_code ec;
    std::filesystem::create_directories(state().host_save_root, ec);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataTerminate() {
    std::lock_guard<std::mutex> lock(state().mutex);
    if (state().is_mounted) {
        fs::vfs_unmount_path("/savedata0");
        state().is_mounted = false;
        state().mount_dir_name.clear();
    }
    state().save_data_initialized = false;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDirNameSearch(const SceSaveDataDirNameSearchCond* cond, SceSaveDataDirNameSearchResult* result) {
    if (!result || !guest_memory::accessible(result, sizeof(*result))) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(state().mutex);
    result->hitNum = 0;
    std::error_code ec;
    if (!std::filesystem::exists(state().host_save_root, ec)) {
        return SCE_OK;
    }

    std::vector<std::string> found_dirs;
    for (const auto& entry : std::filesystem::directory_iterator(state().host_save_root, ec)) {
        if (entry.is_directory(ec)) {
            std::string dname = entry.path().filename().string();
            // If cond filter provided
            if (cond && cond->dirName && cond->dirName->data[0] != '\0') {
                if (dname == cond->dirName->data) {
                    found_dirs.push_back(dname);
                }
            } else {
                found_dirs.push_back(dname);
            }
        }
    }

    result->hitNum = static_cast<std::uint32_t>(found_dirs.size());
    if (cond && result->dirNames && cond->maxEntries > 0) {
        std::size_t count = (std::min)(static_cast<std::size_t>(cond->maxEntries), found_dirs.size());
        if (guest_memory::accessible(result->dirNames, count * sizeof(SceSaveDataDirName))) {
            for (std::size_t i = 0; i < count; ++i) {
                std::memset(result->dirNames[i].data, 0, SCE_SAVE_DATA_DIRNAME_SIZE);
                std::strncpy(result->dirNames[i].data, found_dirs[i].c_str(), SCE_SAVE_DATA_DIRNAME_SIZE - 1);
            }
        }
    }

    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataMount(const SceSaveDataMount* mount, SceSaveDataMountResult* result) {
    if (!mount || !result || !guest_memory::accessible(mount, sizeof(*mount)) || !guest_memory::accessible(result, sizeof(*result))) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(state().mutex);
    char dir_buf[SCE_SAVE_DATA_DIRNAME_SIZE + 1]{};
    std::memcpy(dir_buf, mount->dirName.data, SCE_SAVE_DATA_DIRNAME_SIZE);
    dir_buf[SCE_SAVE_DATA_DIRNAME_SIZE] = '\0';

    std::string dir_name = dir_buf;
    if (dir_name.empty()) {
        dir_name = "savedata0";
    }

    std::filesystem::path host_dir = state().host_save_root / dir_name;
    std::error_code ec;
    std::filesystem::create_directories(host_dir, ec);
    std::filesystem::create_directories(host_dir / "sce_sys", ec);

    // Register mount point in VFS
    fs::vfs_mount_path("/savedata0", host_dir);
    state().is_mounted = true;
    state().mount_dir_name = dir_name;

    std::memset(result->mountPath.data, 0, SCE_SAVE_DATA_MOUNT_POINT_DATA_MAX);
    std::strncpy(result->mountPath.data, "/savedata0", SCE_SAVE_DATA_MOUNT_POINT_DATA_MAX - 1);
    result->requiredBlocks = 0;
    result->reserved = 0;

    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataUmount(const SceSaveDataMountResult* mountResult) {
    (void)mountResult;
    std::lock_guard<std::mutex> lock(state().mutex);
    if (!state().is_mounted) {
        return SCE_SAVE_DATA_ERROR_NOT_MOUNTED;
    }

    fs::vfs_unmount_path("/savedata0");
    state().is_mounted = false;
    state().mount_dir_name.clear();
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDelete(const SceSaveDataDelete* del) {
    if (!del || !guest_memory::accessible(del, sizeof(*del))) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(state().mutex);
    char dir_buf[SCE_SAVE_DATA_DIRNAME_SIZE + 1]{};
    std::memcpy(dir_buf, del->dirName.data, SCE_SAVE_DATA_DIRNAME_SIZE);
    dir_buf[SCE_SAVE_DATA_DIRNAME_SIZE] = '\0';

    std::filesystem::path host_dir = state().host_save_root / dir_buf;
    std::error_code ec;
    std::filesystem::remove_all(host_dir, ec);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetParam(const SceSaveDataMountResult* mountResult, std::uint32_t paramType, const void* param, std::size_t paramSize) {
    (void)mountResult;
    if (!param || paramSize == 0 || !guest_memory::accessible(param, paramSize)) {
        return SCE_OK;
    }
    std::lock_guard<std::mutex> lock(state().mutex);
    if (state().is_mounted && !state().mount_dir_name.empty()) {
        std::filesystem::path sys_dir = state().host_save_root / state().mount_dir_name / "sce_sys";
        std::error_code ec;
        std::filesystem::create_directories(sys_dir, ec);
        std::ofstream out(sys_dir / "param.txt", std::ios::app);
        if (out) {
            out << "Type=" << paramType << " Size=" << paramSize << " Data=";
            const char* str = reinterpret_cast<const char*>(param);
            for (std::size_t i = 0; i < (std::min)(paramSize, std::size_t{128}); ++i) {
                if (str[i] >= 32 && str[i] <= 126) out << str[i];
                else if (str[i] == 0) break;
            }
            out << "\n";
        }
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataSaveIcon(const SceSaveDataMountResult* mountResult, const SceSaveDataIcon* icon) {
    if (!icon || !icon->buf || icon->dataSize == 0 || !guest_memory::accessible(icon->buf, icon->dataSize)) {
        return SCE_OK;
    }
    (void)mountResult;
    std::lock_guard<std::mutex> lock(state().mutex);
    if (state().is_mounted && !state().mount_dir_name.empty()) {
        std::filesystem::path dir = state().host_save_root / state().mount_dir_name;
        std::filesystem::path sys_dir = dir / "sce_sys";
        std::error_code ec;
        std::filesystem::create_directories(sys_dir, ec);
        std::ofstream out(sys_dir / "icon0.png", std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(icon->buf), icon->dataSize);
        }
        std::ofstream out2(dir / "icon0.png", std::ios::binary);
        if (out2) {
            out2.write(reinterpret_cast<const char*>(icon->buf), icon->dataSize);
        }
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetupSaveDataMemory(std::size_t size) {
    std::lock_guard<std::mutex> lock(state().mutex);
    state().memory_save_buffer.resize(size);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataGetSaveDataMemory(SceSaveDataMemory* mem) {
    if (!mem || !guest_memory::accessible(mem, sizeof(*mem))) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(state().mutex);
    mem->buf = state().memory_save_buffer.data();
    mem->bufSize = state().memory_save_buffer.size();
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetSaveDataMemory(const SceSaveDataMemory* mem) {
    if (!mem || !mem->buf || mem->bufSize == 0 || !guest_memory::accessible(mem->buf, mem->bufSize)) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(state().mutex);
    state().memory_save_buffer.assign(reinterpret_cast<const std::uint8_t*>(mem->buf),
                                      reinterpret_cast<const std::uint8_t*>(mem->buf) + mem->bufSize);
    return SCE_OK;
}

// =======================================================================
// libSceSaveDataDialog
// =======================================================================

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogInitialize() {
    std::lock_guard<std::mutex> lock(state().mutex);
    state().dialog_initialized = true;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogTerminate() {
    std::lock_guard<std::mutex> lock(state().mutex);
    state().dialog_initialized = false;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogOpen(const void* params) {
    (void)params;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogClose() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogUpdateStatus() {
    // Return SCE_COMMON_DIALOG_STATUS_FINISHED (3) so callers proceed immediately
    return 3;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogGetResult(SceSaveDataDialogResult* result) {
    if (!result || !guest_memory::accessible(result, sizeof(*result))) {
        return SCE_SAVE_DATA_ERROR_PARAMETER;
    }
    result->result = 0; // SCE_SAVE_DATA_DIALOG_RESULT_OK
    result->buttonId = 0;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogIsReadyToDisplay() {
    return 1;
}

} // extern "C"

} // namespace gravity::hle::savedata
