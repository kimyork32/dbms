#include "storage/disk_storage_engine.hpp"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include "storage/SlottedPage.hpp"
#include "storage/TupleBuilder.hpp"
#include "storage/BPlusTree.hpp"
#include "storage/RecordId.hpp"

namespace megatron {

DiskStorageEngine::DiskStorageEngine() : bpm_(128) {}

bool DiskStorageEngine::CreateTable(const std::string& table_name, const std::vector<std::string>& columns) {
    if (TableExists(table_name)) throw std::runtime_error("table exists");

    Schema schema;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i == 0) { // Asumimos la primera columna como INTEGER y PK
            schema.add_column(columns[i], TypeId::INTEGER);
        } else {
            schema.add_column(columns[i], TypeId::VARCHAR);
        }
    }
    
    schemas_[table_name] = schema;

    std::string db_file = table_name + ".bd";
    std::string index_file = table_name + "_index.db";

    // Limpiar archivos anteriores si existen
    std::remove(db_file.c_str());
    std::remove(index_file.c_str());

    // Crear la primera página de datos a través de BPM
    uint32_t page_id;
    SlottedPage* page = bpm_.NewPage(table_name, &page_id);
    if (page == nullptr) throw std::runtime_error("failed to create table page");
    page->init(0); // Forzar ID 0 para la primera página
    bpm_.UnpinPage(table_name, page_id, true);

    // Inicializar el B+ Tree
    BPlusTreeDisk btree(index_file.c_str());

    return true;
}

bool DiskStorageEngine::TableExists(const std::string& table_name) const {
    return schemas_.find(table_name) != schemas_.end();
}

bool DiskStorageEngine::InsertTuple(const std::string& table_name, const Tuple& tuple) {
    if (!TableExists(table_name)) return false;

    Schema& schema = schemas_[table_name];
    TupleBuilder builder(&schema);
    
    int pk_val = 0;
    for (size_t i = 0; i < tuple.size() && i < schema.columns.size(); ++i) {
        if (schema.columns[i].type == TypeId::INTEGER) {
            std::string val_str = tuple[i];
            if (!val_str.empty() && val_str.front() == '\'') val_str.erase(0, 1);
            if (!val_str.empty() && val_str.back() == '\'') val_str.pop_back();

            int val = 0;
            try {
                val = std::stoi(val_str);
            } catch(...) {}
            builder.set_int(schema.columns[i].name, val);
            if (i == 0) pk_val = val;
        } else {
            std::string val_str = tuple[i];
            if (!val_str.empty() && val_str.front() == '\'') val_str.erase(0, 1);
            if (!val_str.empty() && val_str.back() == '\'') val_str.pop_back();

            builder.set_varchar(schema.columns[i].name, val_str);
        }
    }

    std::string index_file = table_name + "_index.db";

    // Buscar una página con espacio
    uint32_t num_pages = bpm_.GetNumPages(table_name);
    if (num_pages == 0) return false;

    uint32_t current_page_id = num_pages - 1; // Intentamos en la última página
    SlottedPage* page = bpm_.FetchPage(table_name, current_page_id);
    if (!page) return false;

    if (!page->insert_tuple(builder.get_data(), builder.get_size())) {
        // La última página está llena. Creamos una nueva.
        bpm_.UnpinPage(table_name, current_page_id, false);
        
        page = bpm_.NewPage(table_name, &current_page_id);
        if (!page) return false;
        
        if (!page->insert_tuple(builder.get_data(), builder.get_size())) {
            bpm_.UnpinPage(table_name, current_page_id, false);
            return false; // El registro es demasiado grande incluso para una página vacía
        }
    }

    uint16_t slot_id = page->get_header()->num_slots - 1;
    int64_t rid = RecordId::make_rid(page->get_header()->page_id, slot_id);

    BPlusTreeDisk btree(index_file.c_str());
    btree.insert(pk_val, rid);

    bpm_.UnpinPage(table_name, current_page_id, true); // Dirty

    return true;
}

bool DiskStorageEngine::UpdateTuple(const std::string& table_name, const std::string& set_col, const std::string& set_val, const std::string& where_col, const std::string& where_val) {
    if (!TableExists(table_name)) return false;

    Schema& schema = schemas_[table_name];
    std::string index_file = table_name + "_index.db";

    bool using_index = false;
    int search_pk = -1;
    if (where_col == schema.columns[0].name && schema.columns[0].type == TypeId::INTEGER) {
        using_index = true;
        try {
            search_pk = std::stoi(where_val);
        } catch (...) { return false; }
    }

    bool updated = false;

    if (using_index) {
        BPlusTreeDisk btree(index_file.c_str());
        int64_t rid = btree.search(search_pk);
        if (rid != -1) {
            uint32_t p_id = RecordId::get_page_id(rid);
            uint16_t s_id = RecordId::get_slot_id(rid);
            
            SlottedPage* page = bpm_.FetchPage(table_name, p_id);
            if (page) {
                uint16_t tuple_size;
                const char* tuple_data = page->read_tuple(s_id, tuple_size);
                if (tuple_data != nullptr) {
                    Tuple out_tuple;
                    for (size_t col_idx = 0; col_idx < schema.columns.size(); ++col_idx) {
                        const auto& col = schema.columns[col_idx];
                        if (col.type == TypeId::INTEGER) {
                            int32_t val;
                            std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int32_t));
                            out_tuple.push_back(std::to_string(val));
                        } else if (col.is_variable) {
                            uint16_t dir_pos = schema.get_variable_directory_offset() + (col.var_index * 4);
                            uint16_t offset;
                            uint16_t length;
                            std::memcpy(&offset, tuple_data + dir_pos, sizeof(uint16_t));
                            std::memcpy(&length, tuple_data + dir_pos + 2, sizeof(uint16_t));
                            
                            std::string val(tuple_data + offset, length);
                            out_tuple.push_back(val);
                        }
                    }

                    for (size_t col_idx = 0; col_idx < schema.columns.size(); ++col_idx) {
                        if (schema.columns[col_idx].name == set_col) {
                            out_tuple[col_idx] = set_val;
                        }
                    }

                    TupleBuilder builder(&schema);
                    for (size_t i = 0; i < out_tuple.size(); ++i) {
                        if (schema.columns[i].type == TypeId::INTEGER) {
                            int val = std::stoi(out_tuple[i]);
                            builder.set_int(schema.columns[i].name, val);
                        } else {
                            builder.set_varchar(schema.columns[i].name, out_tuple[i]);
                        }
                    }

                    if (page->update_tuple(s_id, builder.get_data(), builder.get_size())) {
                        updated = true;
                    }
                }
                bpm_.UnpinPage(table_name, p_id, updated);
            }
        }
    }
    return updated;
}

bool DiskStorageEngine::DeleteTuple(const std::string& table_name, const std::string& where_col, const std::string& where_val) {
    if (!TableExists(table_name)) return false;

    Schema& schema = schemas_[table_name];
    std::string index_file = table_name + "_index.db";

    bool using_index = false;
    int search_pk = -1;
    if (where_col == schema.columns[0].name && schema.columns[0].type == TypeId::INTEGER) {
        using_index = true;
        try {
            search_pk = std::stoi(where_val);
        } catch (...) { return false; }
    }

    bool deleted = false;

    if (using_index) {
        BPlusTreeDisk btree(index_file.c_str());
        int64_t rid = btree.search(search_pk);
        if (rid != -1) {
            uint32_t p_id = RecordId::get_page_id(rid);
            uint16_t s_id = RecordId::get_slot_id(rid);
            
            SlottedPage* page = bpm_.FetchPage(table_name, p_id);
            if (page) {
                page->delete_tuple(s_id);
                btree.remove(search_pk);
                deleted = true;
                bpm_.UnpinPage(table_name, p_id, true);
            }
        }
    }
    return deleted;
}

std::vector<Tuple> DiskStorageEngine::FullScan(const std::string& table_name) {
    if (!TableExists(table_name)) return {};
    
    Schema& schema = schemas_[table_name];
    std::vector<Tuple> results;
    
    uint32_t num_pages = bpm_.GetNumPages(table_name);

    for (uint32_t p_id = 0; p_id < num_pages; ++p_id) {
        SlottedPage* page = bpm_.FetchPage(table_name, p_id);
        if (!page) continue;

        for (uint16_t i = 0; i < page->get_header()->num_slots; ++i) {
            uint16_t tuple_size;
            const char* tuple_data = page->read_tuple(i, tuple_size);
            if (tuple_data != nullptr) {
                Tuple out_tuple;
                for (size_t col_idx = 0; col_idx < schema.columns.size(); ++col_idx) {
                    const auto& col = schema.columns[col_idx];
                    if (col.type == TypeId::INTEGER) {
                        int32_t val;
                        std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int32_t));
                        out_tuple.push_back(std::to_string(val));
                    } else if (col.is_variable) {
                        uint16_t dir_pos = schema.get_variable_directory_offset() + (col.var_index * 4);
                        uint16_t offset;
                        uint16_t length;
                        std::memcpy(&offset, tuple_data + dir_pos, sizeof(uint16_t));
                        std::memcpy(&length, tuple_data + dir_pos + 2, sizeof(uint16_t));
                        
                        std::string val(tuple_data + offset, length);
                        out_tuple.push_back(val);
                    }
                }
                results.push_back(out_tuple);
            }
        }
        bpm_.UnpinPage(table_name, p_id, false);
    }
    return results;
}

} // namespace megatron
