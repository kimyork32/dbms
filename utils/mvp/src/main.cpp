#include <iostream>
#include <cstdio> // Para remove()

#include "../include/Schema.hpp"
#include "../include/SlottedPage.hpp"
#include "../include/TupleBuilder.hpp"
#include "../include/utils.hpp"
#include "../include/io.hpp"
#include "../include/BPlusTree.hpp"
#include "../include/RecordId.hpp"

int main() {
    // Definir el catalogo
    Schema users;
    users.add_column("id", TypeId::INTEGER);
    users.add_column("nombre", TypeId::VARCHAR);

    // Inicializar B+Tree (limpiamos db anterior si existe para la prueba)
    std::remove("index.db");
    BPlusTreeDisk btree("index.db");

    // Inicializar Slotted Page en memoria
    SlottedPage page;
    uint32_t current_page_id = 100;
    page.init(current_page_id); // page ID 100

    // Simular un INSERT con usuario Ana (id = 1)
    std::cout << "--- INSERTANDO REGISTROS ---\n";
    TupleBuilder builder1(&users);
    builder1.set_int("id", 1);
    builder1.set_varchar("nombre", "Ana");
    if (page.insert_tuple(builder1.get_data(), builder1.get_size())) {
        uint16_t slot_id = page.get_header()->num_slots - 1;
        int64_t rid = RecordId::make_rid(current_page_id, slot_id);
        btree.insert(1, rid);
        std::cout << "Insertado id=1 en [Pagina " << current_page_id << ", Slot " << slot_id << "] (RID: " << rid << ")\n";
    }

    // Simular un INSERT usuario Carlos (id = 2)
    TupleBuilder builder2(&users);
    builder2.set_int("id", 2);
    builder2.set_varchar("nombre", "Carlos");
    if (page.insert_tuple(builder2.get_data(), builder2.get_size())) {
        uint16_t slot_id = page.get_header()->num_slots - 1;
        int64_t rid = RecordId::make_rid(current_page_id, slot_id);
        btree.insert(2, rid);
        std::cout << "Insertado id=2 en [Pagina " << current_page_id << ", Slot " << slot_id << "] (RID: " << rid << ")\n";
    }

    std::cout << "\n--- LEYENDO REGISTRO VIA B+TREE (id = 1) ---\n";
    int64_t search_rid1 = btree.search(1);
    if (search_rid1 != -1) {
        uint32_t p_id = RecordId::get_page_id(search_rid1);
        uint16_t s_id = RecordId::get_slot_id(search_rid1);
        std::cout << "RID encontrado para id=1: [Pagina " << p_id << ", Slot " << s_id << "]\n";

        if (p_id == current_page_id) { // Solo por seguridad en este MVP de una pagina
            uint16_t tuple_size;
            const char* tuple_data = page.read_tuple(s_id, tuple_size);
            if (tuple_data != nullptr) {
                std::cout << "Registro leído: ";
                print_tuple(&users, tuple_data);
            } else {
                std::cout << "Registro id=1 encontrado en el indice pero eliminado de la pagina.\n";
            }
        }
    } else {
        std::cout << "Registro id=1 no encontrado en el indice.\n";
    }

    // Prueba UPDATE (Out-of-place update de Ana)
    std::cout << "\n--- EJECUTANDO UPDATE VIA B+TREE (id=1, Ana -> Ana Maria Rodriguez) ---\n";
    int64_t update_rid = btree.search(1);
    if (update_rid != -1) {
        uint32_t p_id = RecordId::get_page_id(update_rid);
        uint16_t s_id = RecordId::get_slot_id(update_rid);
        
        if (p_id == current_page_id) {
            TupleBuilder builder_update(&users);
            builder_update.set_int("id", 1);
            builder_update.set_varchar("nombre", "Ana Maria Rodriguez");
            
            bool updated = page.update_tuple(s_id, builder_update.get_data(), builder_update.get_size());
            if (updated) {
                std::cout << "Update exitoso en Slot " << s_id << " (Mismo RID guardado)\n";
            } else {
                std::cout << "Error al actualizar (espacio insuficiente o tupla eliminada).\n";
            }
        }
    } else {
        std::cout << "No se puede actualizar, id=1 no encontrado en el indice.\n";
    }

    // Prueba DELETE
    std::cout << "\n--- EJECUTANDO DELETE VIA B+TREE (id=2, Carlos) ---\n";
    int64_t delete_rid = btree.search(2);
    if (delete_rid != -1) {
        uint32_t p_id = RecordId::get_page_id(delete_rid);
        uint16_t s_id = RecordId::get_slot_id(delete_rid);

        if (p_id == current_page_id) {
            page.delete_tuple(s_id);
            btree.remove(2);
            std::cout << "Delete exitoso. Registro eliminado de [Pagina " << p_id << ", Slot " << s_id << "] y del indice.\n";
        }
    } else {
        std::cout << "No se puede eliminar, id=2 no encontrado.\n";
    }

    // Verificamos si existe despues de eliminar
    std::cout << "\n--- LEYENDO REGISTRO ELIMINADO VIA B+TREE (id = 2) ---\n";
    int64_t search_rid2 = btree.search(2);
    if (search_rid2 != -1) {
        std::cout << "Registro id=2 encontrado en indice (esto es un error).\n";
    } else {
        std::cout << "Registro id=2 no encontrado en indice (correcto).\n";
    }

    // Resultado final despues de las operaciones CRUD
    std::cout << "\n--- ESTADO FINAL DE LA PAGINA ---\n";
    std::cout << "Espacio Libre Final: " 
              << (page.get_header()->free_upper - page.get_header()->free_lower) << " bytes\n";
              
    for (uint16_t i = 0; i < page.get_header()->num_slots; ++i) {
        uint16_t tuple_size;
        const char* tuple_data = page.read_tuple(i, tuple_size);
        if (tuple_data != nullptr) {
            std::cout << "Slot " << i << " (tamano " << tuple_size << " bytes): ";
            print_tuple(&users, tuple_data);
        } else {
            std::cout << "Slot " << i << " : [ELIMINADO]\n";
        }
    }

    // Guardar
    std::cout << "\n--- GUARDANDO PAGINA EN DISCO ---\n";
    const char* filename = "data_main2.bd";
    write_page(0, page, filename);
    std::cout << "Pagina guardada exitosamente en " << filename << ".\n";

    return 0;
}
