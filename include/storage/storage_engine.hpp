#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace megatron {

// Representa un valor de columna simple (podría expandirse a una clase más compleja para tipos de datos)
using Value = std::string;
using Tuple = std::vector<Value>;

/**
 * @brief Interfaz base para el motor de almacenamiento.
 * Define las operaciones básicas de acceso a datos que el motor de ejecución requiere.
 */
class IStorageEngine {
public:
    virtual ~IStorageEngine() = default;

    // Gestión de Metadatos
    virtual bool CreateTable(const std::string& table_name, const std::vector<std::string>& columns) = 0;
    virtual bool TableExists(const std::string& table_name) const = 0;

    // Operaciones de Datos
    virtual bool InsertTuple(const std::string& table_name, const Tuple& tuple) = 0;
    
    // El escaneo de tablas en un motor real usaría iteradores (Volcano Model)
    // Aquí definimos una versión simplificada que retorna todas las tuplas.
    virtual std::vector<Tuple> FullScan(const std::string& table_name) = 0;

    // En el futuro, aquí irían TransactionManager, LockManager, etc.
};

} // namespace megatron
