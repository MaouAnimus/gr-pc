#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include "hle/fs/psarc.h"
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace gravity::hle::fs {

// Virtual File System initialization and mounting
bool initialize_vfs(const std::wstring& base_path = L"CUSA01130");
void shutdown_vfs();
std::size_t vfs_mounted_archives_count();
std::size_t vfs_total_files_count();
void vfs_mount_path(std::string_view guest_prefix, const std::filesystem::path& host_path);
void vfs_unmount_path(std::string_view guest_prefix);

// High-level VFS C++ helpers used across the engine and FIOS
int32_t vfs_open(const char* path, int32_t flags = 0, int32_t mode = 0);
int32_t vfs_close(int32_t fd);
int32_t vfs_read(int32_t fd, void* buf, uint32_t size);
int32_t vfs_write(int32_t fd, const void* buf, uint32_t size);
int32_t vfs_pread(int32_t fd, void* buf, uint32_t size, int64_t offset);
int32_t vfs_pwrite(int32_t fd, const void* buf, uint32_t size, int64_t offset);
int64_t vfs_lseek(int32_t fd, int64_t offset, int32_t whence);
int64_t vfs_get_size(int32_t fd);
int64_t vfs_tell(int32_t fd);
int64_t vfs_file_size(const char* path);
bool vfs_exists(const char* path);
bool vfs_is_dir(const char* path);
int32_t vfs_stat(const char* path, void* out_stat);
int32_t vfs_opendir(const char* path);
int32_t vfs_readdir(int32_t dirfd, void* out_entry);
int32_t vfs_closedir(int32_t dirfd);

// HLE functions for libSceFios2 (SysV ABI)
extern "C" {
GRAVITY_SYSV_ABI int32_t sceFios2Open(const char* path, int32_t flags, int32_t mode);
GRAVITY_SYSV_ABI int32_t sceFios2Close(int32_t fd);
GRAVITY_SYSV_ABI int32_t sceFios2Read(int32_t fd, void* buf, uint32_t size);
GRAVITY_SYSV_ABI int32_t sceFios2Write(int32_t fd, const void* buf, uint32_t size);
GRAVITY_SYSV_ABI int64_t sceFios2Lseek(int32_t fd, int64_t offset, int32_t whence);
GRAVITY_SYSV_ABI int32_t sceFios2GetStat(int32_t fd, void* stat);
GRAVITY_SYSV_ABI int32_t sceFios2GetStatByName(const char* path, void* stat);
GRAVITY_SYSV_ABI int32_t sceFios2Opendir(const char* path);
GRAVITY_SYSV_ABI int32_t sceFios2Readdir(int32_t dirfd, void* dirent);
GRAVITY_SYSV_ABI int32_t sceFios2Closedir(int32_t dirfd);
GRAVITY_SYSV_ABI int32_t sceFios2Remove(const char* path);
GRAVITY_SYSV_ABI int32_t sceFios2Mkdir(const char* path, int32_t mode);
GRAVITY_SYSV_ABI int32_t sceFios2Rename(const char* oldpath, const char* newpath);
}

} // namespace gravity::hle::fs