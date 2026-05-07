#include <iostream>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#define SIZE_PAGE 4096

#pragma pack(push, 1)
struct TupleHeader {
    uint8_t flags;
};

struct VarDirEntry {
    uint16_t offset;
    uint16_t length;
};

struct PageHeader {
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_lower;
    uint16_t free_upper;
    uint8_t flags[6];
};
#pragma pack(pop)

class Tuple {
private:
    char* data;

public:
    Tuple(char* record_start_ptr) : data(record_start_ptr) {}

    int32_t get_int(uint16_t fixed_offset) {
        return *reinterpret_cast<int32_t*>(data + fixed_offset);
    }
 
    int16_t get_smallint(uint16_t fixed_offset) {
        return *reinterpret_cast<int16_t*>(data + fixed_offset);
    }

    std::string get_varchar(uint16_t dir_offset) {
        VarDirEntry* dir = reinterpret_cast<VarDirEntry*>(data + dir_offset);
        char* text_ptr = data + dir->offset;
        return std::string(text_ptr, dir->length);
    }


};


class SlottedPage {
public: 
    char data[SIZE_PAGE];


    PageHeader* get_header() {
        return reinterpret_cast<PageHeader*>(data);
    }
};

void write_page(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        std::cerr << "error" << std::endl;
    }

    off_t offset = target_page * SIZE_PAGE;
    size_t bytes_written = pwrite(fd, page.data, SIZE_PAGE, offset);

    if (bytes_written != SIZE_PAGE) {
        std::cerr << "error in writting" << std::endl;
        close(fd);
        return;
    }
    if (fsync(fd) == -1) {
        std::cerr << "error sync with physical disk" << std::endl;
    }
    close(fd);
}

void read_page(uint32_t target_page, SlottedPage& page, const char* filename) {
    int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        std::cerr << "error" << std::endl;
    }

    off_t offset = target_page * SIZE_PAGE;
    size_t bytes_read = pwrite(fd, page.data, SIZE_PAGE, offset);

    if (bytes_read != SIZE_PAGE) {
        std::cerr << "error in writting" << std::endl;
        close(fd);
        return;
    }
    close(fd);
}

int main() {
    // =================================================================
    // EL REGISTRO VARIABLE: (id: 10, nombre: "Carlos")
    // =================================================================
    // Simulamos los 15 bytes extraídos de la Slotted Page
    char registro_variable[15] = {
        0x00,                               // [+0] Tuple header
        0x0A, 0x00, 0x00, 0x00,             // [+1] id = 10 (Fijo, 4 bytes)
        0x09, 0x00, 0x06, 0x00,             // [+5] Dir. 'nombre' (offset: 9, len: 6)
        'C', 'a', 'r', 'l', 'o', 's'        // [+9] Datos variables (6 bytes)
    };

    Tuple tupla_var(registro_variable);

    std::cout << "--- LEYENDO REGISTRO VARIABLE ---\n";
    // Leemos el INT en el offset +1
    std::cout << "ID: " << tupla_var.get_int(1) << "\n";
    // Leemos el VARCHAR cuyo directorio está en el offset +5
    std::cout << "Nombre: " << tupla_var.get_varchar(5) << "\n\n";


    // =================================================================
    // EL REGISTRO FIJO: (id: 11, edad: 30)
    // =================================================================
    // Simulamos los 7 bytes de un registro totalmente fijo
    char registro_fijo[7] = {
        0x00,                               // [+0] Tuple header
        0x0B, 0x00, 0x00, 0x00,             // [+1] id = 11 (Fijo, 4 bytes)
        0x1E, 0x00                          // [+5] edad = 30 (Fijo, 2 bytes)
    };

    Tuple tupla_fija(registro_fijo);

    std::cout << "--- LEYENDO REGISTRO FIJO ---\n";
    // Leemos el INT en el offset +1
    std::cout << "ID: " << tupla_fija.get_int(1) << "\n";
    // Leemos el SMALLINT en el offset +5 (porque 1 + 4 = 5)
    std::cout << "Edad: " << tupla_fija.get_smallint(5) << "\n";

    return 0;
}

// int main() {
//     std::string filename = "data.bd";
//     SlottedPage page_wite;
//
//
//     // read_page(0, page, filename.c_str());
//     // PageHeader* header = page.get_header();
//
//     // std::cout << "--- PÁGINA CARGADA VÍA SYSCALL ---\n";
//     // std::cout << "ID Físico : " << header->page_id << "\n";
//     // std::cout << "Tuplas    : " << header->num_slots << "\n";
//     return 0;
// }


