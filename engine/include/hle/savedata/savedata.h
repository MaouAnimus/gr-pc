#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::savedata {

using SceUserServiceUserId = std::int32_t;
constexpr SceUserServiceUserId SCE_USER_SERVICE_USER_ID_INVALID = -1;
constexpr SceUserServiceUserId SCE_USER_SERVICE_PRIMARY_USER_ID = 0x10000000;

constexpr std::size_t SCE_SAVE_DATA_DIRNAME_SIZE = 32;
constexpr std::size_t SCE_SAVE_DATA_MOUNT_POINT_DATA_MAX = 16;
constexpr std::size_t SCE_USER_SERVICE_MAX_USER_NAME_LENGTH = 16;

struct SceUserServiceInitializeParams {
    std::uint32_t priority;
};

struct SceSaveDataDirName {
    char data[SCE_SAVE_DATA_DIRNAME_SIZE];
};

struct SceSaveDataMountPoint {
    char data[SCE_SAVE_DATA_MOUNT_POINT_DATA_MAX];
};

struct SceSaveDataInitParams {
    std::uint64_t reserved;
};

struct SceSaveDataDirNameSearchCond {
    SceUserServiceUserId userId;
    const SceSaveDataDirName* dirName;
    const char* key;
    std::uint32_t order;
    std::uint32_t maxEntries;
};

struct SceSaveDataDirNameSearchResult {
    std::uint32_t hitNum;
    std::uint32_t reserved;
    SceSaveDataDirName* dirNames;
    void* paramInfo;
};

struct SceSaveDataMount {
    SceUserServiceUserId userId;
    SceSaveDataDirName dirName;
    std::uint64_t blocks;
    std::uint32_t mountMode;
    std::uint32_t reserved;
};

struct SceSaveDataMountResult {
    SceSaveDataMountPoint mountPath;
    std::uint64_t requiredBlocks;
    std::uint32_t reserved;
};

struct SceSaveDataDelete {
    SceUserServiceUserId userId;
    SceSaveDataDirName dirName;
};

struct SceSaveDataIcon {
    const void* buf;
    std::size_t bufSize;
    std::size_t dataSize;
};

struct SceSaveDataMemory {
    void* buf;
    std::size_t bufSize;
};

struct SceSaveDataDialogResult {
    std::int32_t result;
    std::int32_t buttonId;
    std::uint32_t reserved[8];
};

// Host savedata root configuration & status
void set_savedata_root(const char* root_path);
const char* get_savedata_root();
bool is_savedata_mounted();
const char* get_current_mount_dir();

extern "C" {

// libSceUserService
GRAVITY_SYSV_ABI std::int32_t sceUserServiceInitialize(const SceUserServiceInitializeParams* params);
GRAVITY_SYSV_ABI std::int32_t sceUserServiceTerminate();
GRAVITY_SYSV_ABI std::int32_t sceUserServiceGetInitialUser(SceUserServiceUserId* userId);
GRAVITY_SYSV_ABI std::int32_t sceUserServiceGetUserName(SceUserServiceUserId userId, char* userName, std::size_t size);

// libSceSaveData
GRAVITY_SYSV_ABI std::int32_t sceSaveDataInitialize(const SceSaveDataInitParams* params);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataTerminate();
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDirNameSearch(const SceSaveDataDirNameSearchCond* cond, SceSaveDataDirNameSearchResult* result);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataMount(const SceSaveDataMount* mount, SceSaveDataMountResult* result);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataUmount(const SceSaveDataMountResult* mountResult);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDelete(const SceSaveDataDelete* del);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetParam(const SceSaveDataMountResult* mountResult, std::uint32_t paramType, const void* param, std::size_t paramSize);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataSaveIcon(const SceSaveDataMountResult* mountResult, const SceSaveDataIcon* icon);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetupSaveDataMemory(std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataGetSaveDataMemory(SceSaveDataMemory* mem);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataSetSaveDataMemory(const SceSaveDataMemory* mem);

// libSceSaveDataDialog
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogTerminate();
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogOpen(const void* params);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogClose();
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogGetResult(SceSaveDataDialogResult* result);
GRAVITY_SYSV_ABI std::int32_t sceSaveDataDialogIsReadyToDisplay();

}

} // namespace gravity::hle::savedata
