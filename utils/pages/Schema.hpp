#include <vector>
#include <string>
#include <cstdint>

enum class TypeId { 
    INTEGER,  // 4 bytes
    SMALLINT, // 2 bytes
    VARCHAR   // Variable
};

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
    
    // Contadores de la estructura interna del registro
    uint16_t tuple_header_size = 1; // Ej: 1 byte reservado para Null Bitmap / Flags
    uint16_t total_fixed_size = 0;  // Suma total de bytes de tipos fijos (INT, SMALLINT)
    uint16_t num_variable_cols = 0; // Cantidad de columnas dinámicas (VARCHAR)

    // Agrega una columna y recalcula los offsets relativos automáticamente
    void add_column(const std::string& name, TypeId type) {
        Column col;
        col.name = name;
        col.type = type;
        
        if (type == TypeId::VARCHAR) {
            col.is_variable = true;
            col.size = 0; 
            col.fixed_offset = 0; // Las variables no residen en la zona fija
            col.var_index = num_variable_cols++; // Se le asigna un índice para el directorio
        } else {
            col.is_variable = false;
            col.size = (type == TypeId::INTEGER) ? 4 : 2;
            
            // El offset fijo de esta columna es exactamente donde terminan las anteriores
            col.fixed_offset = tuple_header_size + total_fixed_size;
            
            // Incrementamos el tamaño total de la zona fija
            total_fixed_size += col.size;
        }
        
        columns.push_back(col);
    }

    // Retorna el offset exacto donde empieza la Zona de Directorio Variable.
    // Fórmula: Tamaño del Header + Tamaño total de todas las columnas fijas juntas.
    uint16_t get_variable_directory_offset() const {
        return tuple_header_size + total_fixed_size;
    }
};
