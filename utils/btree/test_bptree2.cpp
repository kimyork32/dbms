#include "BPlusTree.h"
#include <iostream>

int main() {
    remove("test2.db");
    BPlusTreeDisk tree("test2.db");
    
    // Insert 1000 elements
    for(int i = 0; i < 1000; i++) {
        tree.insert(i, i * 10);
    }
    
    // Check all elements
    bool ok = true;
    for(int i = 0; i < 1000; i++) {
        if(tree.search(i) != i * 10) {
            std::cout << "Error en search(" << i << ")" << std::endl;
            ok = false;
        }
    }
    if(ok) std::cout << "All inserts and searches OK." << std::endl;
    
    // Remove 500 elements
    for(int i = 0; i < 500; i++) {
        tree.remove(i);
    }
    
    // Check remaining
    ok = true;
    for(int i = 0; i < 500; i++) {
        if(tree.search(i) != -1) {
            std::cout << "Error: found " << i << " after remove" << std::endl;
            ok = false;
        }
    }
    for(int i = 500; i < 1000; i++) {
        if(tree.search(i) != i * 10) {
            std::cout << "Error: " << i << " missing after removes" << std::endl;
            ok = false;
        }
    }
    if(ok) std::cout << "All removes OK." << std::endl;
    
    return 0;
}
