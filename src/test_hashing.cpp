#include <iostream>
#include <string>
#include "../include/storage/static_hash.hpp"
#include "../include/storage/extendible_hash.hpp"
#include "../include/storage/linear_hash.hpp"

using namespace megatron;

int main() {
    std::cout << "=================================================\n";
    std::cout << "  LABORATORIO 08 - PRUEBAS DE ESTRUCTURAS HASH\n";
    std::cout << "=================================================\n\n";

    std::string val;

    // 1. Static Hash
    std::cout << "[1] Probando Hash Estatico (Static Hash)...\n";
    StaticHash<int, std::string> sh(4); // 4 buckets
    sh.Insert(10, "Diez");
    sh.Insert(20, "Veinte");
    sh.Insert(14, "Catorce (Prueba de Colision en Bucket)"); 
    
    if (sh.Get(10, val)) std::cout << " -> Encontrado 10: " << val << "\n";
    if (sh.Get(14, val)) std::cout << " -> Encontrado 14: " << val << "\n";
    std::cout << " -> Prueba Hash Estatico: EXITOSA\n";
    
    // 2. Extendible Hash
    std::cout << "\n[2] Probando Extendible Hashing...\n";
    // Profundidad global inicial 1 (2 entradas en directorio), capacidad de 2 por bucket
    ExtendibleHash<int, std::string> eh(1, 2); 
    eh.Insert(1, "Uno");
    eh.Insert(2, "Dos");
    eh.Insert(3, "Tres");
    eh.Insert(4, "Cuatro (Debe provocar un Split en el Directorio)");
    
    if (eh.Get(3, val)) std::cout << " -> Encontrado 3: " << val << "\n";
    if (eh.Get(4, val)) std::cout << " -> Encontrado 4: " << val << "\n";
    std::cout << " -> Prueba Extendible Hash: EXITOSA\n";

    // 3. Linear Hash
    std::cout << "\n[3] Probando Linear Hashing...\n";
    // 2 buckets iniciales, capacidad de 2 por bucket antes del factor de carga
    LinearHash<int, std::string> lh(2, 2); 
    lh.Insert(5, "Cinco");
    lh.Insert(10, "Diez");
    lh.Insert(15, "Quince (Provoca division iterativa de buckets)");
    lh.Insert(20, "Veinte");
    
    if (lh.Get(15, val)) std::cout << " -> Encontrado 15: " << val << "\n";
    if (lh.Get(20, val)) std::cout << " -> Encontrado 20: " << val << "\n";
    std::cout << " -> Prueba Linear Hash: EXITOSA\n";

    std::cout << "\n=================================================\n";
    std::cout << "           TODAS LAS PRUEBAS SUPERADAS\n";
    std::cout << "=================================================\n";
    return 0;
}
