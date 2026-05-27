#include <iostream>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

#include "storage/SlottedPage.hpp"
#include "storage/io.hpp"

/**
 * provides input and output utilities for persistent storage
 */

/**
 * @brief writes a page to disk
 * @param target_page page number
 * @param page page data
 * @param filename target file
 */
void write_page(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        std::cerr << "error al abrir para escritura\n";
        return;
    }
    off_t offset = target_page * 4096;
    size_t bytes_written = pwrite(fd, page.data, 4096, offset);
    if (bytes_written != 4096) {
        std::cerr << "error en escritura\n";
    }
    if (fsync(fd) == -1) {
        std::cerr << "error en fsync\n";
    }
    close(fd);
}

/**
 * @brief reads a page from disk
 * @param target_page page number
 * @param page page object to fill
 * @param filename source file
 */
void read_page(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        std::cerr << "error al abrir para lectura\n";
        return;
    }
    off_t offset = target_page * 4096;
    size_t bytes_read = pread(fd, page.data, 4096, offset);
    if (bytes_read != 4096) {
        std::cerr << "error en lectura\n";
    }
    close(fd);
}
