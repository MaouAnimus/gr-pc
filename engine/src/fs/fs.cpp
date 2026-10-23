#include "hle/fs.h"
#include "hle/fs/psarc.h"
#include <mutex>
#include <filesystem>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstring>

namespace gravity::hle::fs {

namespace {

struct VfsFile {
    bool is_psarc{false};
    std::vector<std::uint8_t> data{};
    std::size_t offset{0};
    HANDLE win_handle{INVALID_HANDLE_VALUE};
};

class VfsManager {
public:
    static VfsManager& instance() {
        static VfsManager mgr;
        return mgr;
    }

    bool initialize(const std::wstring& base_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialize_locked(base_path);
    }

    // Caller holds mutex_; shared by explicit and lazy initialization.
    bool initialize_locked(const std::wstring& base_path) {
        if (initialized_) return true;

        std::filesystem::path candidate(base_path);
        const char* relative_candidates[] = {
            "CUSA01130",
            "../CUSA01130",
            "../../CUSA01130"
        };

        if (!std::filesystem::exists(candidate)) {
            for (const char* rel : relative_candidates) {
                if (std::filesystem::exists(rel)) {
                    candidate = std::filesystem::path(rel);
                    break;
                }
            }
        }

        base_dir_ = candidate;

        // Mount all .psarc archives in <base_dir>/arc/
        std::filesystem::path arc_dir = base_dir_ / "arc";
        total_files_ = 0;

        if (std::filesystem::exists(arc_dir) && std::filesystem::is_directory(arc_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(arc_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".psarc") {
                    auto arc = std::make_unique<PsarcArchive>();
                    if (arc->open(entry.path().wstring())) {
                        total_files_ += arc->file_count();
                        archives_.push_back(std::move(arc));
                    }
                }
            }
        }

        initialized_ = true;
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [fd, file] : open_files_) {
            if (!file.is_psarc && file.win_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(file.win_handle);
            }
        }
        open_files_.clear();
        archives_.clear();
        total_files_ = 0;
        initialized_ = false;
    }

    int32_t open(const char* path, int32_t flags, int32_t mode) {
        (void)mode;
        if (!path || (flags & 3)) return -1; // /app0 is read-only.

        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return -1;
        }

        std::string_view p(path);
        std::string norm = PsarcArchive::normalize_path(p);
        const std::filesystem::path relative(norm);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
            norm.find(':') != std::string::npos) return -1;
        for (const auto& component : relative)
            if (component == "..") return -1;

        // 1. Try to open from mounted PSARC archives
        for (const auto& arc : archives_) {
            const auto* entry = arc->get_entry(norm);
            if (entry) {
                VfsFile vf{};
                vf.is_psarc = true;
                vf.data.resize(static_cast<std::size_t>(entry->uncompressed_size));
                if (arc->read_file(*entry, vf.data.data(), vf.data.size())) {
                    int32_t fd = next_fd_++;
                    open_files_[fd] = std::move(vf);
                    return fd;
                }
            }
        }

        // 2. Fallback: try host disk under base_dir_
        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / relative);
        const auto inside = real_path.lexically_relative(root);
        if (inside.empty() || inside.is_absolute()) return -1;
        for (const auto& component : inside)
            if (component == "..") return -1;
        if (std::filesystem::exists(real_path)) {
            DWORD access = GENERIC_READ;
            if (flags & 0x0002) access |= GENERIC_WRITE; // O_RDWR
            if (flags & 0x0001) access = GENERIC_WRITE;  // O_WRONLY

            HANDLE h = CreateFileW(real_path.wstring().c_str(), access, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                VfsFile vf{};
                vf.is_psarc = false;
                vf.win_handle = h;
                int32_t fd = next_fd_++;
                open_files_[fd] = std::move(vf);
                return fd;
            }
        }

        return -1;
    }

    int32_t close(int32_t fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        if (!it->second.is_psarc && it->second.win_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(it->second.win_handle);
        }
        open_files_.erase(it);
        return 0;
    }

    int32_t read(int32_t fd, void* buf, uint32_t size) {
        if (!buf || size == 0) return 0;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        VfsFile& vf = it->second;
        if (vf.is_psarc) {
            if (vf.offset >= vf.data.size()) {
                return 0; // EOF
            }
            std::size_t avail = vf.data.size() - vf.offset;
            std::size_t to_read = (std::min)(static_cast<std::size_t>(size), avail);
            std::memcpy(buf, vf.data.data() + vf.offset, to_read);
            vf.offset += to_read;
            return static_cast<int32_t>(to_read);
        } else {
            DWORD bytes_read = 0;
            if (ReadFile(vf.win_handle, buf, size, &bytes_read, nullptr)) {
                return static_cast<int32_t>(bytes_read);
            }
            return -1;
        }
    }

    int64_t lseek(int32_t fd, int64_t offset, int32_t whence) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        VfsFile& vf = it->second;
        if (vf.is_psarc) {
            std::int64_t new_off = 0;
            if (whence == 0) { // SEEK_SET
                new_off = offset;
            } else if (whence == 1) { // SEEK_CUR
                new_off = static_cast<std::int64_t>(vf.offset) + offset;
            } else if (whence == 2) { // SEEK_END
                new_off = static_cast<std::int64_t>(vf.data.size()) + offset;
            } else {
                return -1;
            }

            if (new_off < 0) return -1;
            vf.offset = static_cast<std::size_t>(new_off);
            return static_cast<int64_t>(vf.offset);
        } else {
            LARGE_INTEGER li{};
            li.QuadPart = offset;
            LARGE_INTEGER new_li{};
            DWORD method = FILE_BEGIN;
            if (whence == 1) method = FILE_CURRENT;
            if (whence == 2) method = FILE_END;

            if (SetFilePointerEx(vf.win_handle, li, &new_li, method)) {
                return new_li.QuadPart;
            }
            return -1;
        }
    }

    [[nodiscard]] std::size_t mounted_archives_count() const noexcept {
        return archives_.size();
    }

    [[nodiscard]] std::size_t total_files_count() const noexcept {
        return total_files_;
    }

private:
    std::mutex mutex_;
    bool initialized_{false};
    std::filesystem::path base_dir_{"CUSA01130"};
    std::vector<std::unique_ptr<PsarcArchive>> archives_{};
    std::unordered_map<int32_t, VfsFile> open_files_{};
    int32_t next_fd_{10};
    std::size_t total_files_{0};
};

} // anonymous namespace

bool initialize_vfs(const std::wstring& base_path) {
    return VfsManager::instance().initialize(base_path);
}

void shutdown_vfs() {
    VfsManager::instance().shutdown();
}

std::size_t vfs_mounted_archives_count() {
    return VfsManager::instance().mounted_archives_count();
}

std::size_t vfs_total_files_count() {
    return VfsManager::instance().total_files_count();
}

extern "C" {

GRAVITY_SYSV_ABI int32_t sceFios2Open(const char* path, int32_t flags, int32_t mode) {
    return VfsManager::instance().open(path, flags, mode);
}

GRAVITY_SYSV_ABI int32_t sceFios2Close(int32_t fd) {
    return VfsManager::instance().close(fd);
}

GRAVITY_SYSV_ABI int32_t sceFios2Read(int32_t fd, void* buf, uint32_t size) {
    return VfsManager::instance().read(fd, buf, size);
}

GRAVITY_SYSV_ABI int32_t sceFios2Write(int32_t fd, const void* buf, uint32_t size) {
    (void)fd; (void)buf; (void)size;
    return -1; // Read-only VFS for archives
}

GRAVITY_SYSV_ABI int64_t sceFios2Lseek(int32_t fd, int64_t offset, int32_t whence) {
    return VfsManager::instance().lseek(fd, offset, whence);
}

GRAVITY_SYSV_ABI int32_t sceFios2GetStat(int32_t fd, void* stat) {
    (void)fd; (void)stat;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2GetStatByName(const char* path, void* stat) {
    (void)path; (void)stat;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2Opendir(const char* path) {
    (void)path;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2Readdir(int32_t dirfd, void* dirent) {
    (void)dirfd; (void)dirent;
    return -1; // End of directory
}

GRAVITY_SYSV_ABI int32_t sceFios2Closedir(int32_t dirfd) {
    (void)dirfd;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2Remove(const char* path) {
    (void)path;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2Mkdir(const char* path, int32_t mode) {
    (void)path; (void)mode;
    return -1;
}

GRAVITY_SYSV_ABI int32_t sceFios2Rename(const char* oldpath, const char* newpath) {
    (void)oldpath; (void)newpath;
    return -1;
}

} // extern "C"

} // namespace gravity::hle::fs
