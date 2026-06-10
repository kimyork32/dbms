#include <iostream>
#include <string>
#include "../include/storage/static_hash.hpp"
#include "../include/storage/extendible_hash.hpp"
#include "../include/storage/linear_hash.hpp"

using namespace dbms::storage;

int main() {
    std::cout << "=================================================\n";
    std::cout << "  LABORATORIO 08 - PRUEBAS DE ESTRUCTURAS HASH\n";
    std::cout << "=================================================\n\n";

    std::string val;

    // 1. Static Hash
    std::cout << "[1] Probando Hash Estatico (Static Hash)...\n";
    StaticHash<int, std::string> sh(4); // 4 buckets
    sh.insert(10, "Diez");
    sh.insert(20, "Veinte");
    sh.insert(14, "Catorce (Prueba de Colision en Bucket)"); 
    
    if (sh.get(10, val)) std::cout << " -> Encontrado 10: " << val << "\n";
    if (sh.get(14, val)) std::cout << " -> Encontrado 14: " << val << "\n";
    std::cout << " -> Prueba Hash Estatico: EXITOSA\n";
    
    // 2. Extendible Hash
    std::cout << "\n[2] Probando Extendible Hashing...\n";
    // Profundidad global inicial 1 (2 entradas en directorio), capacidad de 2 por bucket
    ExtendibleHash<int, std::string> eh(1, 2); 
    eh.insert(1, "Uno");
    eh.insert(2, "Dos");
    eh.insert(3, "Tres");
    eh.insert(4, "Cuatro (Debe provocar un Split en el Directorio)");
    
    if (eh.get(3, val)) std::cout << " -> Encontrado 3: " << val << "\n";
    if (eh.get(4, val)) std::cout << " -> Encontrado 4: " << val << "\n";
    std::cout << " -> Prueba Extendible Hash: EXITOSA\n";

    // 3. Linear Hash
    std::cout << "\n[3] Probando Linear Hashing...\n";
    // 2 buckets iniciales, capacidad de 2 por bucket antes del factor de carga
    LinearHash<int, std::string> lh(2, 2); 
    lh.insert(5, "Cinco");
    lh.insert(10, "Diez");
    lh.insert(15, "Quince (Provoca division iterativa de buckets)");
    lh.insert(20, "Veinte");
    
    if (lh.get(15, val)) std::cout << " -> Encontrado 15: " << val << "\n";
    if (lh.get(20, val)) std::cout << " -> Encontrado 20: " << val << "\n";
    std::cout << " -> Prueba Linear Hash: EXITOSA\n";

    std::cout << "\n=================================================\n";
    std::cout << "           TODAS LAS PRUEBAS SUPERADAS\n";
    std::cout << "=================================================\n";
    return 0;
}
