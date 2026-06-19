#include <iostream>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

#include "storage/slotted_page.hpp"
#include "storage/io.hpp"

namespace megatron {

void WritePage(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        std::cerr << "error opening for writing\n";
        return;
    }
    off_t offset = target_page * 4096;
    size_t bytes_written = pwrite(fd, page.data, 4096, offset);
    if (bytes_written != 4096) {
        std::cerr << "error in writing\n";
    }
    if (fsync(fd) == -1) {
        std::cerr << "error in fsync\n";
    }
    close(fd);
}

void ReadPage(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        std::cerr << "error opening for reading\n";
        return;
    }
    off_t offset = target_page * 4096;
    size_t bytes_read = pread(fd, page.data, 4096, offset);
    if (bytes_read != 4096) {
        std::cerr << "error in reading\n";
    }
    close(fd);
}

} // namespace megatron
