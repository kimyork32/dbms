#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

// ============================================================
// CATALOGO DEL SISTEMA (SCHEMA)
// ============================================================
enum class TypeId { INTEGER, SMALLINT, VARCHAR };

struct Column {
    std::string name;
    TypeId type;
    uint16_t size;
    uint16_t fixed_offset;
    bool is_variable;
    uint16_t var_index;
};

class Schema {
public:
    std::vector<Column> columns;
    uint16_t tuple_header_size = 1; 
    uint16_t total_fixed_size = 0;
    uint16_t num_variable_cols = 0;

    void add_column(const std::string& name, TypeId type) {
        Column col;
        col.name = name;
        col.type = type;
        
        if (type == TypeId::VARCHAR) {
            col.is_variable = true;
            col.size = 0; 
            col.fixed_offset = 0; 
            col.var_index = num_variable_cols++;
        } else {
            col.is_variable = false;
            col.size = (type == TypeId::INTEGER) ? 4 : 2;
            col.fixed_offset = tuple_header_size + total_fixed_size;
            total_fixed_size += col.size;
        }
        columns.push_back(col);
    }

    uint16_t get_variable_directory_offset() const {
        return tuple_header_size + total_fixed_size;
    }
};

// ============================================================
// CONSTRUCTOR DE REGISTROS (TUPLE BUILDER)
// ============================================================
class TupleBuilder {
private:
    const Schema* schema;
    std::vector<char> buffer;
    uint16_t current_var_offset;

public:
    TupleBuilder(const Schema* s) : schema(s) {
        uint16_t base_size = schema->tuple_header_size + 
                             schema->total_fixed_size + 
                             (schema->num_variable_cols * 4);
        buffer.assign(base_size, 0);
        current_var_offset = base_size;
    }

    void set_int(const std::string& col_name, int32_t value) {
        for (const auto& col : schema->columns) {
            if (col.name == col_name && col.type == TypeId::INTEGER) {
                std::memcpy(buffer.data() + col.fixed_offset, &value, sizeof(int32_t));
                return;
            }
        }
    }

    void set_varchar(const std::string& col_name, const std::string& value) {
        for (const auto& col : schema->columns) {
            if (col.name == col_name && col.is_variable) {
                uint16_t text_length = value.size();
                uint16_t dir_pos = schema->get_variable_directory_offset() + (col.var_index * 4);

                std::memcpy(buffer.data() + dir_pos, &current_var_offset, sizeof(uint16_t));
                std::memcpy(buffer.data() + dir_pos + 2, &text_length, sizeof(uint16_t));

                buffer.insert(buffer.end(), value.begin(), value.end());
                current_var_offset += text_length;
                return;
            }
        }
    }

    const char* get_data() const { return buffer.data(); }
    uint16_t get_size() const { return buffer.size(); }
};

// ============================================================
// ESTRUCTURAS FISICAS DE LA PÁGINA (SLOTTED PAGE)
// ============================================================
#pragma pack(push, 1)
struct Slot {
    uint16_t offset;
    uint16_t length;
};

struct PageHeader {
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_lower; // crece hacia abajo (inicia en 16)
    uint16_t free_upper; // crece hacia arriba (inicia en 4096)
    uint8_t  flags[6];
};
#pragma pack(pop)

class SlottedPage {
public:
    char data[4096]; // bloque fisico o page

    PageHeader* get_header() {
        return reinterpret_cast<PageHeader*>(data);
    }

    Slot* get_slot(uint16_t slot_index) {
        return reinterpret_cast<Slot*>(data + sizeof(PageHeader) + (slot_index * sizeof(Slot)));
    }

    void init(uint32_t page_id) {
        PageHeader* header = get_header();
        header->page_id = page_id;
        header->num_slots = 0;
        header->free_lower = sizeof(PageHeader);
        header->free_upper = 4096;
        for(int i=0; i<6; i++) header->flags[i] = 0;
    }

    // CREATE
    bool insert_tuple(const char* tuple_data, uint16_t size) {
        PageHeader* header = get_header();
        
        // verificar si hay espacio (tamano registro + 4 bytes del slot)
        if (header->free_upper - header->free_lower < size + sizeof(Slot)) {
            return false; // Memoria Insuficiente
        }

        // calcular nuevo offset (hacia arriba)
        uint16_t new_offset = header->free_upper - size;

        // copiar los datos del registro en la zona fisica
        std::memcpy(data + new_offset, tuple_data, size);

        // crear la entrada en el Slot Directory
        Slot* new_slot = get_slot(header->num_slots);
        new_slot->offset = new_offset;
        new_slot->length = size;

        // actualizar punteros del Header
        header->free_upper = new_offset;
        header->free_lower += sizeof(Slot);
        header->num_slots++;

        return true;
    }

    // READ
    const char* read_tuple(uint16_t slot_id, uint16_t& out_size) {
        PageHeader* header = get_header();
        if (slot_id >= header->num_slots) return nullptr;

        Slot* slot = get_slot(slot_id);
        if (slot->length == 0) return nullptr; // registro ya eliminado

        out_size = slot->length;
        return data + slot->offset;
    }

    // ========================================================
    // COMPACTACIÓN (VACUUM)
    // =======================================================
    void compact() {
        char temp_data[4096];
        uint16_t current_upper = 4096;
        PageHeader* header = get_header();

        // mover todos los registros vivos al fondo del buffer temporal
        for (uint16_t i = 0; i < header->num_slots; ++i) {
            Slot* slot = get_slot(i);
            if (slot->length > 0) { // si el registro NO está eliminado
                current_upper -= slot->length;
                std::memcpy(temp_data + current_upper, data + slot->offset, slot->length);
                
                // actualizar el offset al nuevo lugar fisico
                slot->offset = current_upper;
            }
        }

        // copiar el bloque compactado de vuelta a la pagina real
        uint16_t bytes_to_copy = 4096 - current_upper;
        if (bytes_to_copy > 0) {
            std::memcpy(data + current_upper, temp_data + current_upper, bytes_to_copy);
        }

        // actualizar el limite superior del espacio libre
        header->free_upper = current_upper;
    }

    // =======================================================
    // DELETE
    // =======================================================
    void delete_tuple(uint16_t slot_id) {
        PageHeader* header = get_header();
        if (slot_id >= header->num_slots) return;

        Slot* slot = get_slot(slot_id);
        if (slot->length == 0) return; // ya estaba eliminado
        
        // marcado logico
        slot->length = 0; 
        
        // compactar el espacio fragmentado
        compact();
    }

    // =======================================================
    // UPDATE
    // =======================================================
    bool update_tuple(uint16_t slot_id, const char* new_data, uint16_t new_size) {
        PageHeader* header = get_header();
        if (slot_id >= header->num_slots) return false;

        Slot* slot = get_slot(slot_id);
        if (slot->length == 0) return false; // no se puede actualizar un registro eliminado

        // CASO 1: In-place update (El nuevo registro cabe en el espacio actual)
        if (new_size <= slot->length) {
            std::memcpy(data + slot->offset, new_data, new_size);
            slot->length = new_size; // actualizar el tamaño por si se redujo
            return true;
        }

        // CASO 2: Out-of-place update (El nuevo registro es más grande)
        // verificar si hay espacio. Si no hay, entonces forzar compactacion
        if (header->free_upper - header->free_lower < new_size) {
            compact();
            // si despues de compactar sigue sin haber espacio, falla
            if (header->free_upper - header->free_lower < new_size) {
                return false; 
            }
        }

        // calcular la nueva posicion en el espacio libre
        uint16_t new_offset = header->free_upper - new_size;
        
        // copiar los nuevos datos
        std::memcpy(data + new_offset, new_data, new_size);
        
        // actualizar el Slot existente (mantiene su ID pero cambia de puntero)
        slot->offset = new_offset;
        slot->length = new_size;
        
        // actualizar el espacio libre
        header->free_upper = new_offset;
        
        return true; 
    }
};

// ===========================================================
// UTILERIA PARA LEER REGISTROS
// ===========================================================
void print_tuple(const Schema* schema, const char* tuple_data) {
    std::cout << "{ ";
    for (size_t i = 0; i < schema->columns.size(); ++i) {
        const auto& col = schema->columns[i];
        std::cout << col.name << ": ";
        
        if (col.type == TypeId::INTEGER) {
            int32_t val;
            std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int32_t));
            std::cout << val;
        } else if (col.type == TypeId::SMALLINT) {
            int16_t val;
            std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int16_t));
            std::cout << val;
        } else if (col.is_variable) {
            uint16_t dir_pos = schema->get_variable_directory_offset() + (col.var_index * 4);
            uint16_t offset;
            uint16_t length;
            std::memcpy(&offset, tuple_data + dir_pos, sizeof(uint16_t));
            std::memcpy(&length, tuple_data + dir_pos + 2, sizeof(uint16_t));
            
            std::string val(tuple_data + offset, length);
            std::cout << "\"" << val << "\"";
        }
        
        if (i < schema->columns.size() - 1) std::cout << ", ";
    }
    std::cout << " }\n";
}

// ===========================================================
// I/O PÁGINAS (WRITE / READ)
// ===========================================================
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

// ===========================================================
// MAIN
// ===========================================================
int main() {
    // definir el catalogo
    Schema users;
    users.add_column("id", TypeId::INTEGER);
    users.add_column("nombre", TypeId::VARCHAR);

    // inicializar Slotted Page en memoria
    SlottedPage page;
    page.init(100); // page ID 100

    // simular un INSERT con usuario Ana
    TupleBuilder builder1(&users);
    builder1.set_int("id", 1);
    builder1.set_varchar("nombre", "Ana");
    page.insert_tuple(builder1.get_data(), builder1.get_size());

    // simular un INSERT usuario Carlos
    TupleBuilder builder2(&users);
    builder2.set_int("id", 2);
    builder2.set_varchar("nombre", "Carlos");
    page.insert_tuple(builder2.get_data(), builder2.get_size());

    std::cout << "page inicializada e inserciones completadas.\n";
    std::cout << "Tuplas actuales: " << page.get_header()->num_slots << "\n";

    // imprimir los registros insertados en la pagina
    std::cout << "\n--- ESTADO INICIAL (READ) ---\n";
    uint16_t num_slots = page.get_header()->num_slots;
    for (uint16_t i = 0; i < num_slots; ++i) {
        uint16_t tuple_size;
        const char* tuple_data = page.read_tuple(i, tuple_size);
        if (tuple_data != nullptr) {
            std::cout << "Slot " << i << " (tamano " << tuple_size << " bytes): ";
            print_tuple(&users, tuple_data);
        }
    }

    // prueba update (Out-of-place update)
    std::cout << "\n--- EJECUTANDO UPDATE (Ana -> Ana Maria Rodriguez) ---\n";
    TupleBuilder builder_update(&users);
    builder_update.set_int("id", 1);
    builder_update.set_varchar("nombre", "Ana Maria Rodriguez");
    
    // Actualizamos el Slot 0 (Ana)
    bool updated = page.update_tuple(0, builder_update.get_data(), builder_update.get_size());
    if (updated) std::cout << "update exitoso\n";

    // prueba delete
    std::cout << "\n--- EJECUTANDO DELETE (Carlos) ---\n";
    page.delete_tuple(1); // Eliminamos el Slot 1 (Carlos)
    std::cout << "Delete exitoso. Pagina compactada.\n";

    // resultado final despues de las operaciones CRUD
    std::cout << "\n--- ESTADO FINAL DE LA PAGINA ---\n";
    std::cout << "Espacio Libre Final: " 
              << (page.get_header()->free_upper - page.get_header()->free_lower) << " bytes\n";
              
    for (uint16_t i = 0; i < page.get_header()->num_slots; ++i) {
        uint16_t tuple_size;
        const char* tuple_data = page.read_tuple(i, tuple_size);
        if (tuple_data != nullptr) {
            std::cout << "Slot " << i << " (tamaño " << tuple_size << " bytes): ";
            print_tuple(&users, tuple_data);
        } else {
            std::cout << "Slot " << i << " : [ELIMINADO]\n";
        }
    }

    // simular archivo binario (I/O) usando write_page
    std::cout << "\n--- GUARDANDO PAGINA EN DISCO ---\n";
    const char* filename = "data_main2.bd";
    write_page(0, page, filename);
    std::cout << "Pagina guardada exitosamente en " << filename << ".\n";

    // prueba de read_page
    std::cout << "\n--- LEYENDO PAGINA DESDE DISCO ---\n";
    SlottedPage page_read;
    read_page(0, page_read, filename);
    std::cout << "ID Fisico leido: " << page_read.get_header()->page_id << "\n";
    std::cout << "Tuplas leidas: " << page_read.get_header()->num_slots << "\n";

    return 0;
}
