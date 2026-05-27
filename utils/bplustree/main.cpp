#include <iostream>
#include "BPlusTree.h"

int main() {
    std::cout << "init b+tree in disk...\n";
    remove("test.db"); // beging with a clean db
    
    BPlusTreeDisk btree("test.db");

    std::cout << "inserting values...\n";
    btree.insert(10, 100);
    btree.insert(20, 200);
    btree.insert(5, 50);
    btree.insert(6, 60);
    btree.insert(12, 120);
    btree.insert(30, 300);
    btree.insert(7, 70);
    btree.insert(17, 170);

    std::cout << "seaching values:\n";
    int keys_to_search[] = {10, 20, 5, 6, 12, 30, 7, 17, 99};
    for(int k : keys_to_search) {
        int64_t val = btree.search(k);
        if (val != -1) {
            std::cout << "Clave " << k << " encontrada con valor: " << val << "\n";
        } else {
            std::cout << "Clave " << k << " no encontrada.\n";
        }
    }

    std::cout << "Eliminando clave 12...\n";
    btree.remove(12);
    
    std::cout << "Buscando clave 12 tras eliminación: ";
    int64_t val = btree.search(12);
    if (val != -1) {
        std::cout << "Encontrada (" << val << ")\n";
    } else {
        std::cout << "No encontrada.\n";
    }

    return 0;
}
