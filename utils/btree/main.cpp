#include <iostream>
#include "BTree.h"

int main() {
    std::cout << "BTree with grade 3" << std::endl;
    
    // max key per node = 3
    BTree<int, 3> btree;
    
    int data[] = {10, 20, 5, 6, 12, 30, 7, 17};
    for(int v : data) {
        std::cout << "inserted: " << v << std::endl;
        btree.insert(v);
    }

    std::cout << "\nroute of B-Tree:";
    btree.print();

    return 0;
}
