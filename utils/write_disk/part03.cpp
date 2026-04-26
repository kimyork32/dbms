#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

constexpr size_t PAGE_SIZE = 4096;

using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE_ID = -1;

class Page {
private:
    std::vector<uint8_t> data;
    page_id_t pageId;

public:
    // constructor
    // inicializa el vector con zero-fill garantizado
    explicit Page(page_id_t id = INVALID_PAGE_ID) : data(PAGE_SIZE, 0), pageId(id) {}

    // inyeccion de bytes crudos con bounds checking
    void writeData(const void* src, size_t offset, size_t len) {
        if (offset + len > PAGE_SIZE) {
            throw std::out_of_range("writeData: buffer overflow evitado (offset + len > 4096)");
        }
        // aritmetica de punteros subyacente para el salto de memoria
        std::memcpy(data.data() + offset, src, len);
    }

    // extraccion de bytes crudos
    void readData(void* dest, size_t offset, size_t len) const {
        if (offset + len > PAGE_SIZE) {
            throw std::out_of_range("readData: segmentacion de memoria evitada");
        }
        std::memcpy(dest, data.data() + offset, len);
    }

    uint32_t getPageId() const {
        return static_cast<uint32_t>(pageId);
    }

    // Zero-fill explicito para sanitizacion del frame de memoria
    void reset() {
        pageId = INVALID_PAGE_ID;
        std::memset(data.data(), 0, PAGE_SIZE);
    }
    
    // Metodo auxiliar para obtener el puntero crudo
    const uint8_t* getRawData() const {
        return data.data();
    }
};

int main() {
    Page page(5); // Pagina logica #5
    
    // escribir un entero (4 bytes) en el offset 100
    int32_t value_to_write = 42;
    page.writeData(&value_to_write, 100, sizeof(int32_t));
    
    // escribir una cadena en el offset 104
    std::string text = "DBMS_Record";
    page.writeData(text.c_str(), 104, text.length() + 1);

    // leer los datos de vuelta
    int32_t read_value = 0;
    page.readData(&read_value, 100, sizeof(int32_t));
    
    char buffer[20] = {0};
    page.readData(buffer, 104, text.length() + 1);
    
    std::cout << "pagina ID: " << page.getPageId() << "\n";
    std::cout << "valor extraido (offset 100): " << read_value << "\n";
    std::cout << "texto extraido (offset 104): " << buffer << "\n";

    // limpiar el frame para su reuso por otra transaccion
    page.reset();
    
    return 0;
}
