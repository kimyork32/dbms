#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

constexpr size_t PAGE_SIZE = 4096;
constexpr int TOTAL_PAGES = 10;

void writePages(const std::string& filename) {
    int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        std::cerr << "error en open() al crear archivo\n";
        return;
    }

    std::vector<uint8_t> buffer(PAGE_SIZE);

    for (int i = 0; i < TOTAL_PAGES; ++i) {
        std::fill(buffer.begin(), buffer.end(), static_cast<uint8_t>(i));
        
        off_t offset = static_cast<off_t>(i) * PAGE_SIZE;

        // buffer.data() devuelve el puntero crudo (uint8_t*) subyacente
        ssize_t written = pwrite(fd, buffer.data(), PAGE_SIZE, offset);
        
        if (written != PAGE_SIZE) {
            std::cerr << "error de escritura parcial o fallo de disco en pwrite()\n";
        }
    }
    
    close(fd);
    std::cout << "se escribieron " << TOTAL_PAGES << " paginas usando pwrite.\n";
}

size_t calculateTotalPages(const std::string& filename) {
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "error en open() para lectura\n";
        return 0;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        std::cerr << "error en fstat()\n";
        close(fd);
        return 0;
    }

    close(fd);
    return static_cast<size_t>(file_stat.st_size / PAGE_SIZE);
}

int main() {
    std::string dbName = "data.db";
    writePages(dbName);
    
    size_t total = calculateTotalPages(dbName);
    std::cout << "pages: " << total << "\n";
    
    return 0;
}
