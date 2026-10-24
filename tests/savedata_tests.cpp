#include "hle/savedata/savedata.h"
#include "hle/fs.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::savedata;
using namespace gravity::hle::fs;

int main(int argc, char* argv[]) {
    std::string suite = (argc > 1) ? argv[1] : "all";

    if (suite == "user_service" || suite == "all") {
        SceUserServiceInitializeParams params{0};
        CHECK(sceUserServiceInitialize(&params) == 0);

        SceUserServiceUserId user = -1;
        CHECK(sceUserServiceGetInitialUser(&user) == 0);
        CHECK(user == SCE_USER_SERVICE_PRIMARY_USER_ID);

        char name[32]{};
        CHECK(sceUserServiceGetUserName(user, name, sizeof(name)) == 0);
        CHECK(std::strcmp(name, "Player") == 0);

        CHECK(sceUserServiceTerminate() == 0);
        std::cout << "[PASS] user_service tests succeeded.\n";
    }

    if (suite == "save_mount_vfs" || suite == "all") {
        std::error_code ec;
        std::filesystem::remove_all("test_savedata", ec);
        set_savedata_root("test_savedata");

        SceSaveDataInitParams init_params{};
        CHECK(sceSaveDataInitialize(&init_params) == 0);

        SceSaveDataMount mount{};
        mount.userId = SCE_USER_SERVICE_PRIMARY_USER_ID;
        std::strncpy(mount.dirName.data, "SLOT00", sizeof(mount.dirName.data) - 1);
        SceSaveDataMountResult mount_res{};
        CHECK(sceSaveDataMount(&mount, &mount_res) == 0);
        CHECK(std::strcmp(mount_res.mountPath.data, "/savedata0") == 0);
        CHECK(is_savedata_mounted());

        // Test VFS writing to mounted path
        int32_t fd = vfs_open("/savedata0/savegame.bin", 0x0201, 0); // O_WRONLY | O_CREAT
        CHECK(fd >= 0);
        const char test_data[] = "GRAVITY_RUSH_SAVE_TEST_DATA_12345";
        CHECK(vfs_write(fd, test_data, sizeof(test_data)) == sizeof(test_data));
        CHECK(vfs_close(fd) == 0);

        // Test VFS reading back from mounted path
        fd = vfs_open("/savedata0/savegame.bin", 0, 0);
        CHECK(fd >= 0);
        char read_buf[64]{};
        CHECK(vfs_read(fd, read_buf, sizeof(read_buf)) == sizeof(test_data));
        CHECK(std::strcmp(read_buf, test_data) == 0);
        CHECK(vfs_close(fd) == 0);

        // Test directory search
        SceSaveDataDirNameSearchCond search_cond{};
        search_cond.userId = SCE_USER_SERVICE_PRIMARY_USER_ID;
        search_cond.maxEntries = 10;
        SceSaveDataDirName out_dirs[10]{};
        SceSaveDataDirNameSearchResult search_res{};
        search_res.dirNames = out_dirs;
        CHECK(sceSaveDataDirNameSearch(&search_cond, &search_res) == 0);
        CHECK(search_res.hitNum >= 1);
        CHECK(std::strcmp(out_dirs[0].data, "SLOT00") == 0);

        // Test param setting
        const char save_title[] = "Episode 1: Awakening";
        CHECK(sceSaveDataSetParam(&mount_res, 0, save_title, sizeof(save_title)) == 0);

        // Test icon saving
        const uint8_t fake_png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        SceSaveDataIcon icon{};
        icon.buf = fake_png;
        icon.dataSize = sizeof(fake_png);
        CHECK(sceSaveDataSaveIcon(&mount_res, &icon) == 0);
        CHECK(std::filesystem::exists("test_savedata/SLOT00/sce_sys/icon0.png"));
        CHECK(std::filesystem::exists("test_savedata/SLOT00/sce_sys/param.txt"));

        // Test memory save buffers
        CHECK(sceSaveDataSetupSaveDataMemory(1024) == 0);
        SceSaveDataMemory mem{};
        CHECK(sceSaveDataGetSaveDataMemory(&mem) == 0);
        CHECK(mem.buf != nullptr && mem.bufSize == 1024);
        char mem_data[] = "MEMORY_BLOCK_DATA";
        SceSaveDataMemory set_mem{};
        set_mem.buf = mem_data;
        set_mem.bufSize = sizeof(mem_data);
        CHECK(sceSaveDataSetSaveDataMemory(&set_mem) == 0);
        CHECK(sceSaveDataGetSaveDataMemory(&mem) == 0);
        CHECK(mem.bufSize == sizeof(mem_data));

        // Test umount
        CHECK(sceSaveDataUmount(&mount_res) == 0);
        CHECK(!is_savedata_mounted());

        CHECK(sceSaveDataTerminate() == 0);
        std::filesystem::remove_all("test_savedata", ec);
        std::cout << "[PASS] save_mount_vfs tests succeeded.\n";
    }

    if (suite == "save_dialog" || suite == "all") {
        CHECK(sceSaveDataDialogInitialize() == 0);
        CHECK(sceSaveDataDialogOpen(nullptr) == 0);
        CHECK(sceSaveDataDialogUpdateStatus() == 3); // Finished
        CHECK(sceSaveDataDialogIsReadyToDisplay() == 1);

        SceSaveDataDialogResult res{};
        CHECK(sceSaveDataDialogGetResult(&res) == 0);
        CHECK(res.result == 0); // OK
        CHECK(res.buttonId == 0);

        CHECK(sceSaveDataDialogClose() == 0);
        CHECK(sceSaveDataDialogTerminate() == 0);
        std::cout << "[PASS] save_dialog tests succeeded.\n";
    }

    return 0;
}
