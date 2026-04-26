#include <iostream>
// #include "BTree.h"
#include "BTreeCormen.h"

int main() {
    // // for max keys = 3, minimum grade t will be 2 (2t - 1 = 3)
    // std::cout << "BTree with minimum degree t=2" << std::endl;
    //
    // // max key per node = 3
    // BTree<int, 2> btree; 
    //
    // int data[] = {10, 20, 5, 6, 12, 30, 7, 17};
    // for(int v : data) {
    //     std::cout << "inserted: " << v << std::endl;
    //     btree.insert(v);
    // }
    //
    // std::cout << "\nroute of B-Tree: ";
    // btree.print();

    BTree<int, 3> btree;

    std::cout << "Insertando\n";
    btree.insert(4);
    btree.insert(8);
    btree.insert(3);
    btree.insert(5);
    btree.insert(2);

    std::cout << "imprimiendo\n";
    btree.print();

    return 0;
}
