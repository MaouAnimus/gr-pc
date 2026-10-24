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
        open_dirs_.clear();
        next_dirfd_ = 100;
        archives_.clear();
        custom_mounts_.clear();
        total_files_ = 0;
        initialized_ = false;
    }

    void mount_path(std::string_view guest_prefix, const std::filesystem::path& host_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_mounts_[std::string(guest_prefix)] = host_path;
    }

    void unmount_path(std::string_view guest_prefix) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_mounts_.erase(std::string(guest_prefix));
    }

    std::optional<std::filesystem::path> resolve_custom_path_locked(std::string_view p) {
        for (const auto& [prefix, host_root] : custom_mounts_) {
            if (p == prefix || (p.starts_with(prefix) && (p.size() == prefix.size() || p[prefix.size()] == '/'))) {
                std::string_view sub = p.substr(prefix.size());
                while (!sub.empty() && sub.front() == '/') sub.remove_prefix(1);
                std::filesystem::path sub_p(sub);
                for (const auto& comp : sub_p) {
                    if (comp == "..") return std::nullopt;
                }
                return host_root / sub_p;
            }
        }
        return std::nullopt;
    }

    int32_t open(const char* path, int32_t flags, int32_t mode) {
        (void)mode;
        if (!path) return -1;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return -1;
        }

        std::string_view p(path);
        if (auto custom = resolve_custom_path_locked(p)) {
            const auto real_path = *custom;
            std::error_code ec;
            if (flags & 0x0200) {
                std::filesystem::create_directories(real_path.parent_path(), ec);
            }

            DWORD access = GENERIC_READ;
            if ((flags & 3) == 1) access = GENERIC_WRITE;
            else if ((flags & 3) == 2) access = GENERIC_READ | GENERIC_WRITE;

            DWORD creation = OPEN_EXISTING;
            if (flags & 0x0200) {
                if (flags & 0x0800) creation = CREATE_NEW;
                else if (flags & 0x0400) creation = CREATE_ALWAYS;
                else creation = OPEN_ALWAYS;
            } else if (flags & 0x0400) {
                creation = TRUNCATE_EXISTING;
            }

            HANDLE h = CreateFileW(real_path.wstring().c_str(), access,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                VfsFile vf{};
                vf.is_psarc = false;
                vf.win_handle = h;
                int32_t fd = next_fd_++;
                open_files_[fd] = std::move(vf);
                return fd;
            }
            return -1;
        }

        if (flags & 3) return -1; // /app0 is read-only.
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

    int32_t write(int32_t fd, const void* buf, uint32_t size) {
        if (!buf || size == 0) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end() || it->second.is_psarc || it->second.win_handle == INVALID_HANDLE_VALUE) return -1;
        DWORD bytes_written = 0;
        if (WriteFile(it->second.win_handle, buf, size, &bytes_written, nullptr)) {
            return static_cast<int32_t>(bytes_written);
        }
        return -1;
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

    int32_t pread(int32_t fd, void* buf, uint32_t size, int64_t offset) {
        if (!buf || size == 0 || offset < 0) return 0;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        VfsFile& vf = it->second;
        if (vf.is_psarc) {
            if (static_cast<std::size_t>(offset) >= vf.data.size()) {
                return 0; // EOF
            }
            std::size_t avail = vf.data.size() - static_cast<std::size_t>(offset);
            std::size_t to_read = (std::min)(static_cast<std::size_t>(size), avail);
            std::memcpy(buf, vf.data.data() + offset, to_read);
            return static_cast<int32_t>(to_read);
        } else {
            LARGE_INTEGER cur_pos{};
            LARGE_INTEGER zero{};
            if (!SetFilePointerEx(vf.win_handle, zero, &cur_pos, FILE_CURRENT)) return -1;

            LARGE_INTEGER target{};
            target.QuadPart = offset;
            if (!SetFilePointerEx(vf.win_handle, target, nullptr, FILE_BEGIN)) return -1;

            DWORD bytes_read = 0;
            BOOL ok = ReadFile(vf.win_handle, buf, size, &bytes_read, nullptr);
            SetFilePointerEx(vf.win_handle, cur_pos, nullptr, FILE_BEGIN);
            return ok ? static_cast<int32_t>(bytes_read) : -1;
        }
    }

    int32_t pwrite(int32_t fd, const void* buf, uint32_t size, int64_t offset) {
        if (!buf || size == 0 || offset < 0) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end() || it->second.is_psarc || it->second.win_handle == INVALID_HANDLE_VALUE) return -1;
        LARGE_INTEGER cur_pos{}, zero{};
        if (!SetFilePointerEx(it->second.win_handle, zero, &cur_pos, FILE_CURRENT)) return -1;
        LARGE_INTEGER target{};
        target.QuadPart = offset;
        if (!SetFilePointerEx(it->second.win_handle, target, nullptr, FILE_BEGIN)) return -1;
        DWORD bytes_written = 0;
        BOOL ok = WriteFile(it->second.win_handle, buf, size, &bytes_written, nullptr);
        SetFilePointerEx(it->second.win_handle, cur_pos, nullptr, FILE_BEGIN);
        return ok ? static_cast<int32_t>(bytes_written) : -1;
    }

    int64_t get_size(int32_t fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        VfsFile& vf = it->second;
        if (vf.is_psarc) {
            return static_cast<int64_t>(vf.data.size());
        } else {
            LARGE_INTEGER li{};
            if (GetFileSizeEx(vf.win_handle, &li)) {
                return li.QuadPart;
            }
            return -1;
        }
    }

    int64_t tell(int32_t fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_files_.find(fd);
        if (it == open_files_.end()) return -1;

        VfsFile& vf = it->second;
        if (vf.is_psarc) {
            return static_cast<int64_t>(vf.offset);
        } else {
            LARGE_INTEGER zero{}, cur{};
            if (SetFilePointerEx(vf.win_handle, zero, &cur, FILE_CURRENT)) {
                return cur.QuadPart;
            }
            return -1;
        }
    }

    int64_t file_size(const char* path) {
        if (!path) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return -1;
        }

        std::string_view p(path);
        if (auto custom = resolve_custom_path_locked(p)) {
            std::error_code ec;
            if (std::filesystem::exists(*custom, ec) && std::filesystem::is_regular_file(*custom, ec)) {
                return static_cast<int64_t>(std::filesystem::file_size(*custom, ec));
            }
            return -1;
        }

        std::string norm = PsarcArchive::normalize_path(p);
        for (const auto& arc : archives_) {
            const auto* entry = arc->get_entry(norm);
            if (entry) {
                return static_cast<int64_t>(entry->uncompressed_size);
            }
        }

        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / norm);
        std::error_code ec;
        if (std::filesystem::exists(real_path, ec) && std::filesystem::is_regular_file(real_path, ec)) {
            return static_cast<int64_t>(std::filesystem::file_size(real_path, ec));
        }
        return -1;
    }

    bool exists(const char* path) {
        if (!path) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return false;
        }

        std::string_view p(path);
        if (auto custom = resolve_custom_path_locked(p)) {
            std::error_code ec;
            return std::filesystem::exists(*custom, ec);
        }

        std::string norm = PsarcArchive::normalize_path(p);
        for (const auto& arc : archives_) {
            if (arc->get_entry(norm)) return true;
        }

        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / norm);
        std::error_code ec;
        return std::filesystem::exists(real_path, ec);
    }

    bool is_dir(const char* path) {
        if (!path) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return false;
        }

        std::string_view p(path);
        if (auto custom = resolve_custom_path_locked(p)) {
            std::error_code ec;
            return std::filesystem::is_directory(*custom, ec);
        }

        std::string norm = PsarcArchive::normalize_path(p);
        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / norm);
        std::error_code ec;
        if (std::filesystem::is_directory(real_path, ec)) return true;

        std::string prefix = norm;
        if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
        for (const auto& arc : archives_) {
            for (const auto& entry : arc->entries()) {
                if (entry.path.starts_with(prefix)) return true;
            }
        }
        return false;
    }

    int32_t stat(const char* path, void* out_stat) {
        if (!path || !out_stat) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return -1;
        }

        std::string_view p(path);
        struct RawStat {
            int64_t size{0};
            int64_t ctime{0};
            int64_t mtime{0};
            int64_t atime{0};
            int64_t res20[5]{};
            int64_t mode{0};
        };
        auto* st = static_cast<RawStat*>(out_stat);
        std::memset(st, 0, sizeof(RawStat));

        if (auto custom = resolve_custom_path_locked(p)) {
            std::error_code ec;
            auto fs_status = std::filesystem::status(*custom, ec);
            if (ec || !std::filesystem::exists(fs_status)) return -1;
            if (std::filesystem::is_directory(fs_status)) {
                st->size = 0;
                st->mode = 0040755;
            } else {
                st->size = static_cast<int64_t>(std::filesystem::file_size(*custom, ec));
                st->mode = 0100644;
            }
            auto lwt = std::filesystem::last_write_time(*custom, ec);
            if (!ec) {
                auto sct = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                    std::chrono::file_clock::to_sys(lwt)).time_since_epoch().count();
                st->ctime = st->mtime = st->atime = sct;
            }
            return 0;
        }

        std::string norm = PsarcArchive::normalize_path(p);

        for (const auto& arc : archives_) {
            const auto* entry = arc->get_entry(norm);
            if (entry) {
                st->size = static_cast<int64_t>(entry->uncompressed_size);
                st->mode = 0100644; // S_IFREG | 0644
                st->ctime = st->mtime = st->atime = 1600000000LL * 1000000000LL;
                return 0;
            }
        }

        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / norm);
        std::error_code ec;
        auto fs_status = std::filesystem::status(real_path, ec);
        if (ec || !std::filesystem::exists(fs_status)) {
            // Check if virtual directory in PSARC
            std::string prefix = norm;
            if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
            for (const auto& arc : archives_) {
                for (const auto& entry : arc->entries()) {
                    if (entry.path.starts_with(prefix)) {
                        st->size = 0;
                        st->mode = 0040755; // S_IFDIR | 0755
                        return 0;
                    }
                }
            }
            return -1;
        }

        if (std::filesystem::is_directory(fs_status)) {
            st->size = 0;
            st->mode = 0040755;
        } else {
            st->size = static_cast<int64_t>(std::filesystem::file_size(real_path, ec));
            st->mode = 0100644;
        }
        auto lwt = std::filesystem::last_write_time(real_path, ec);
        if (!ec) {
            auto sct = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                std::chrono::file_clock::to_sys(lwt)).time_since_epoch().count();
            st->ctime = st->mtime = st->atime = sct;
        }
        return 0;
    }

    int32_t opendir(const char* path) {
        if (!path) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            if (!initialize_locked(L"CUSA01130")) return -1;
        }

        std::string_view p(path);
        VfsDir dir{};
        if (auto custom = resolve_custom_path_locked(p)) {
            std::error_code ec;
            if (std::filesystem::exists(*custom, ec) && std::filesystem::is_directory(*custom, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(*custom, ec)) {
                    dir.entries.push_back({
                        entry.path().filename().string(),
                        entry.is_directory(ec),
                        entry.is_regular_file(ec) ? static_cast<int64_t>(entry.file_size(ec)) : 0
                    });
                }
            }
            int32_t dirfd = next_dirfd_++;
            open_dirs_[dirfd] = std::move(dir);
            return dirfd;
        }

        std::string norm = PsarcArchive::normalize_path(p);

        const auto root = std::filesystem::weakly_canonical(base_dir_);
        const auto real_path = std::filesystem::weakly_canonical(root / norm);
        std::error_code ec;
        if (std::filesystem::exists(real_path, ec) && std::filesystem::is_directory(real_path, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(real_path, ec)) {
                dir.entries.push_back({
                    entry.path().filename().string(),
                    entry.is_directory(ec),
                    entry.is_regular_file(ec) ? static_cast<int64_t>(entry.file_size(ec)) : 0
                });
            }
        }

        // Also add unique entries from mounted archives matching the prefix
        std::string prefix = norm;
        if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
        for (const auto& arc : archives_) {
            for (const auto& entry : arc->entries()) {
                if (entry.path.starts_with(prefix)) {
                    std::string_view sub = std::string_view(entry.path).substr(prefix.size());
                    auto slash = sub.find('/');
                    std::string child_name = (slash == std::string_view::npos) ? std::string(sub) : std::string(sub.substr(0, slash));
                    bool is_subdir = (slash != std::string_view::npos);
                    bool already = false;
                    for (const auto& existing : dir.entries) {
                        if (existing.name == child_name) {
                            already = true;
                            break;
                        }
                    }
                    if (!already && !child_name.empty()) {
                        dir.entries.push_back({
                            child_name,
                            is_subdir,
                            is_subdir ? 0 : static_cast<int64_t>(entry.uncompressed_size)
                        });
                    }
                }
            }
        }

        int32_t dfd = next_dirfd_++;
        open_dirs_[dfd] = std::move(dir);
        return dfd;
    }

    int32_t readdir(int32_t dirfd, void* out_entry) {
        if (!out_entry) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_dirs_.find(dirfd);
        if (it == open_dirs_.end()) return -1;

        VfsDir& dir = it->second;
        if (dir.index >= dir.entries.size()) {
            return -1; // End of directory
        }

        const auto& item = dir.entries[dir.index++];
        struct RawDirEntry {
            int64_t size{0};
            int64_t ctime{0};
            int64_t mtime{0};
            int64_t atime{0};
            int64_t res20[5]{};
            int64_t mode{0};
            char name[968]{};
        };
        auto* entry = static_cast<RawDirEntry*>(out_entry);
        std::memset(entry, 0, sizeof(RawDirEntry));
        entry->size = item.size;
        entry->mode = item.is_directory ? 0040755 : 0100644;
        std::strncpy(entry->name, item.name.c_str(), sizeof(entry->name) - 1);
        return 0;
    }

    int32_t closedir(int32_t dirfd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_dirs_.find(dirfd);
        if (it == open_dirs_.end()) return -1;
        open_dirs_.erase(it);
        return 0;
    }

    [[nodiscard]] std::size_t mounted_archives_count() const noexcept {
        return archives_.size();
    }

    [[nodiscard]] std::size_t total_files_count() const noexcept {
        return total_files_;
    }

private:
    struct VfsDirEntryInfo {
        std::string name;
        bool is_directory{false};
        int64_t size{0};
    };
    struct VfsDir {
        std::vector<VfsDirEntryInfo> entries;
        std::size_t index{0};
    };

    std::mutex mutex_;
    bool initialized_{false};
    std::filesystem::path base_dir_{"CUSA01130"};
    std::vector<std::unique_ptr<PsarcArchive>> archives_{};
    std::unordered_map<int32_t, VfsFile> open_files_{};
    std::unordered_map<int32_t, VfsDir> open_dirs_{};
    std::unordered_map<std::string, std::filesystem::path> custom_mounts_{};
    int32_t next_fd_{10};
    int32_t next_dirfd_{100};
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

void vfs_mount_path(std::string_view guest_prefix, const std::filesystem::path& host_path) {
    VfsManager::instance().mount_path(guest_prefix, host_path);
}

void vfs_unmount_path(std::string_view guest_prefix) {
    VfsManager::instance().unmount_path(guest_prefix);
}

int32_t vfs_open(const char* path, int32_t flags, int32_t mode) {
    return VfsManager::instance().open(path, flags, mode);
}

int32_t vfs_close(int32_t fd) {
    return VfsManager::instance().close(fd);
}

int32_t vfs_read(int32_t fd, void* buf, uint32_t size) {
    return VfsManager::instance().read(fd, buf, size);
}

int32_t vfs_write(int32_t fd, const void* buf, uint32_t size) {
    return VfsManager::instance().write(fd, buf, size);
}

int32_t vfs_pread(int32_t fd, void* buf, uint32_t size, int64_t offset) {
    return VfsManager::instance().pread(fd, buf, size, offset);
}

int32_t vfs_pwrite(int32_t fd, const void* buf, uint32_t size, int64_t offset) {
    return VfsManager::instance().pwrite(fd, buf, size, offset);
}

int64_t vfs_lseek(int32_t fd, int64_t offset, int32_t whence) {
    return VfsManager::instance().lseek(fd, offset, whence);
}

int64_t vfs_get_size(int32_t fd) {
    return VfsManager::instance().get_size(fd);
}

int64_t vfs_tell(int32_t fd) {
    return VfsManager::instance().tell(fd);
}

int64_t vfs_file_size(const char* path) {
    return VfsManager::instance().file_size(path);
}

bool vfs_exists(const char* path) {
    return VfsManager::instance().exists(path);
}

bool vfs_is_dir(const char* path) {
    return VfsManager::instance().is_dir(path);
}

int32_t vfs_stat(const char* path, void* out_stat) {
    return VfsManager::instance().stat(path, out_stat);
}

int32_t vfs_opendir(const char* path) {
    return VfsManager::instance().opendir(path);
}

int32_t vfs_readdir(int32_t dirfd, void* out_entry) {
    return VfsManager::instance().readdir(dirfd, out_entry);
}

int32_t vfs_closedir(int32_t dirfd) {
    return VfsManager::instance().closedir(dirfd);
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
    return VfsManager::instance().write(fd, buf, size);
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

