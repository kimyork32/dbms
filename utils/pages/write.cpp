#include <iostream>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define SIZE_PAGE 4096

#pragma pack(push, 1)
struct PageHeader {
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_lower;
    uint16_t free_upper;
    uint8_t flags[6];
};
#pragma pack (pop)


class SlottedPage {
public: 
    char data[SIZE_PAGE];
    PageHeader* get_header() {
        return reinterpret_cast<PageHeader*>(data);
    }
};



int main() {
    SlottedPage page;
    int fd = open("data.bd", O_RDONLY);

    if (fd == -1) {
        std::cerr << "error" << std::endl;
    }

    uint32_t target_page = 1;
    off_t offset = target_page * SIZE_PAGE;
    ssize_t bytes_read = pread(fd, page.data, SIZE_PAGE, offset);

    if (bytes_read != SIZE_PAGE) {
        std::cerr << "error I/O in reading" << std::endl;
        close(fd);
        return 1;
    }

    PageHeader* header = page.get_header();

    std::cout << "--- PÁGINA CARGADA VÍA SYSCALL ---\n";
    std::cout << "ID Físico : " << header->page_id << "\n";
    std::cout << "Tuplas    : " << header->num_slots << "\n";

    close(fd);
    return 0;
}


