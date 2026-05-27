#include "BPlusTree.h"
#include <iostream>

int main() {
    remove("test.db");
    BPlusTreeDisk tree("test.db");
    tree.insert(10, 100);
    tree.insert(20, 200);
    tree.insert(5, 50);
    std::cout << "search 10: " << tree.search(10) << std::endl;
    std::cout << "search 20: " << tree.search(20) << std::endl;
    std::cout << "search 5: " << tree.search(5) << std::endl;
    std::cout << "search 15: " << tree.search(15) << std::endl;
    tree.remove(10);
    std::cout << "search 10 after remove: " << tree.search(10) << std::endl;
    return 0;
}
