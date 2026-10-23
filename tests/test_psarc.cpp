#include <iostream>
#include <iomanip>
#include <vector>
#include "hle/fs.h"

int main() {
    std::cout << "Testing PSARC reader..." << std::endl;

    // Open directory /app0/
    int dirfd = sceFios2Opendir("/app0/");
    if (dirfd < 0) {
        std::cerr << "Failed to open directory /app0/" << std::endl;
        return 1;
    }

    std::cout << "Directory opened successfully (fd=" << dirfd << ")." << std::endl;

    // Buffer for dirent
    struct dirent* ent = nullptr;
    // We need to allocate space for dirent; we can use a static buffer on stack.
    // We'll call sceFios2Readdir which expects a pointer to a dirent.
    // We'll allocate a dirent on the stack.
    dirent entbuf;
    while (true) {
        int res = sceFios2Readdir(dirfd, &entbuf);
        if (res <= 0) {
            // 0 means end of directory, negative means error
            if (res == 0) {
                std::cout << "End of directory reached." << std::endl;
            } else {
                std::cerr << "Error reading directory: " << res << std::endl;
            }
            break;
        }
        const dirent* ent = &entbuf;
        if (ent->d_type == DT_REG) {
            std::string filename(ent->d_name);
            std::cout << "Found file: " << filename << std::endl;
            // Open the file
            int fd = sceFios2Open(("/app0/" + filename).c_str(), O_RDONLY, 0);
            if (fd < 0) {
                std::cerr << "  Failed to open file: " << fd << std::endl;
                continue;
            }
            std::cout << "  Opened file (fd=" << fd << ")." << std::endl;
            // Read up to 32 bytes
            std::vector<uint8_t> buffer(32);
            ssize_t nread = sceFios2Read(fd, buffer.data(), buffer.size());
            if (nread < 0) {
                std::cerr << "  Read error: " << nread << std::endl;
            } else if (nread == 0) {
                std::cout << "  File is empty." << std::endl;
            } else {
                std::cout << "  First " << nread << " bytes (hex): ";
                for (size_t i = 0; i < static_cast<size_t>(nread); ++i) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(buffer[i]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
            sceFios2Close(fd);
            std::cout << "  Closed file." << std::endl;
        } else if (ent->d_type == DT_DIR) {
            std::cout << "Found directory: " << ent->d_name << std::endl;
        } else {
            std::cout << "Found entry of type " << static_cast<int>(ent->d_type) << ": " << ent->d_name << std::endl;
        }
    }

    sceFios2Closedir(dirfd);
    std::cout << "Directory closed." << std::endl;

    std::cout << "Test completed." << std::endl;
    return 0;
}