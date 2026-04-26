#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

constexpr size_t PAGE_SIZE = 4096;
constexpr int TOTAL_PAGES = 10;

// alignas(8) fuerza que la estructura inicie en una dir mem multiplo de 8.
struct alignas(8) PageHeader {
    uint32_t page_id;    // 4 bytes
    uint16_t flags;      // 2 bytes
                         // -> 2 bytes de PADDING implicito insertados por el compilador
    uint64_t lsn;        // 8 bytes
}; // total = 16 bytes

// reserva de memoria manual
void demonstrateManualMemory() {
    uint8_t* raw_buffer = new uint8_t[PAGE_SIZE]; // Asignacion en el Heap
    
    // memory leak (Fuga de memoria)
    // si ocurre un error aquí y la función retorna sin ejecutar delete[], 
    // el sistema operativo pierde la referencia a estos 4096 bytes hasta que el proceso muera
    
    delete[] raw_buffer; // Liberación obligatoria
}

void writePages(const std::string& filename) {
    int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        std::cerr << "error en open()\n";
        return;
    }

    // garantiza memoria contigua
    // se libera automaticamente al salir del scope de la func
    std::vector<uint8_t> buffer(PAGE_SIZE);

    for (int i = 0; i < TOTAL_PAGES; ++i) {
        std::fill(buffer.begin(), buffer.end(), static_cast<uint8_t>(i));
        
        // escribir el header alineado al inicio del buffer
        PageHeader header = {static_cast<uint32_t>(i), 0, 1000};
        std::copy(reinterpret_cast<uint8_t*>(&header), 
                  reinterpret_cast<uint8_t*>(&header) + sizeof(PageHeader), 
                  buffer.begin());

        off_t offset = static_cast<off_t>(i) * PAGE_SIZE;
        
        ssize_t written = pwrite(fd, buffer.data(), PAGE_SIZE, offset);
        
        if (written != PAGE_SIZE) {
            std::cerr << "error de escritura en pwrite()\n";
        }
    }
    
    close(fd);
}

size_t calculateTotalPages(const std::string& filename) {
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) return 0;

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        close(fd);
        return 0;
    }

    close(fd);
    return static_cast<size_t>(file_stat.st_size / PAGE_SIZE);
}

int main() {
    demonstrateManualMemory();
    
    std::string dbName = "data.db";
    writePages(dbName);
    
    std::cout << "pages: " << calculateTotalPages(dbName) << "\n";
    return 0;
}
