#ifndef BTREE_CORMEN
#define BTREE_CORMEN

#include <stdint.h>
#include <stddef.h>
#include <utility>
#include <cstring>
#include <iostream>

template <class T, int t = 3>
struct BNode {
    bool leaf;
    size_t num_keys;
    T keys[2 * t - 1];
    BNode* children[2 * t];

    BNode(bool leaf = true) {
        this->leaf = leaf;
        this->num_keys = 0;
        std::memset(children, 0, sizeof(children));
    }
};

template <class T, int t = 3>
class BTree {
private: 
    BNode<T, t>* root;

    size_t binarySearch(BNode<T, t>* node, T key) {
        int low = 0;
        int high = node->num_keys - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (node->keys[mid] == key) return mid;
            if (node->keys[mid] < key) low = mid + 1;
            else high = mid - 1;
        }
        return low; // not found, but key is contained in x.c_low
    }

    void splitChild(BNode<T, t>* x, size_t i) {
        BNode<T, t>* y = x->children[i];    // full node to split
        BNode<T, t>* z = new BNode<T, t>(y->leaf);  // z will take half of y
        z->num_keys = t - 1;
        std::memcpy(z->keys, &y->keys[t], (t - 1) * sizeof(T));
        if (!y->leaf) {
            std::memcpy(z->children, &y->children[t], t * sizeof(BNode<T, t>*));
        }
        y->num_keys = t - 1;
        std::memmove(&x->children[i + 2], &x->children[i + 1], 
                (x->num_keys - i) * sizeof(BNode<T, t>*));
        x->children[i + 1] = z;
        std::memmove(&x->keys[i + 1], &x->keys[i], (x->num_keys - i) * sizeof(T));
        x->keys[i] = y->keys[t - 1];
        x->num_keys++;
        // diskWrite(y)
        // diskWrite(z)
        // diskWrite(x)
    }

    BNode<T, t>* splitRoot(BNode<T, t>* node) {
        BNode<T, t>* s = new BNode<T, t>(false);
        s->children[0] = root;
        this->root = s;
        splitChild(s, 0);
        return s;
    }

    void insertNonFull(BNode<T, t>* x, T key) {
        if (x->leaf) {
            size_t i = binarySearch(x, key);
            std::memmove(&x->keys[i + 1], &x->keys[i], (x->num_keys - i) * sizeof(T));
            x->keys[i] = key;
            ++x->num_keys;
            // diskWrite(x)
        }
        else {  // find the child where k belongs
            size_t i = binarySearch(x, key);
            // diskRead(x.ci)
            if (x->children[i]->num_keys == 2 * t - 1) {
                splitChild(x, i);
                if (key > x->keys[i]) ++i;
            }
            insertNonFull(x->children[i], key);
        }
    }

    T getPredecessor(BNode<T, t>* x) {
        if (x->leaf) return x->keys[x->num_keys - 1];
        return getPredecessor(x->children[x->num_keys]);
    }

    T getSucessor(BNode<T, t>* x) {
        if (x->leaf) return x->keys[0];
        return getSucessor(x->children[0]);
    }

    void removeInternal(BNode<T, t>* x, T key) {
        // case 1: leaf node
        if (x->leaf) {
            size_t i = binarySearch(x, key);
            if (i < x->num_keys && x->keys[i] == key) {
                // mark as deleted or completely eliminate... (this is the case)
                std::memmove(&x->keys[i], &x->keys[i + 1], (x->num_keys - i - 1) * sizeof(T));
                --x->num_keys;
            }
        }
        // case 2: internal node
        else {
            size_t i = binarySearch(x, key);
            // diskRead(x.ci)
            // diskRead(x.ci+1)
            // this internal node contain key
            if (i < x->num_keys && x->keys[i] == key) {
                // case 2a: x.ci has at least t keys
                if (x->children[i]->num_keys >= t) {
                    T p = getPredecessor(x->children[i]);
                    x->keys[i] = p;
                    removeInternal(x->children[i], p);
                }
                // case 2b: x.ci has t - 1 keys and x.ci+1 has at least t keys
                else if (x->children[i + 1]->num_keys >= t){
                    T s = getSucessor(x->children[i + 1]);
                    x->keys[i] = s;
                    removeInternal(x->children[i + 1], s);
                }
                // case 2c: both x.ci and x.ci+1 has t - 1 keys
                else {
                    BNode<T, t>* y = x->children[i];
                    BNode<T, t>* z = x->children[i + 1];
                    y->keys[t - 1] = x->keys[i];
                    std::memcpy(&y->keys[t], &z->keys[0], (t - 1) * sizeof(T));
                    if (!y->leaf) {
                        std::memcpy(&y->children[t], &z->children[0], t * sizeof(BNode<T, t>*));
                    }
                    y->num_keys = 2 * t - 1;
                    std::memmove(&x->keys[i], &x->keys[i + 1], (x->num_keys - i - 1) * sizeof(T));
                    std::memmove(&x->children[i + 1], &x->children[i + 2], (x->num_keys - i - 1) * sizeof(BNode<T, t>*));
                    x->num_keys--;
                    delete z;
                    removeInternal(y, key);
                }
            }
            // case 3: internal node no contain key
            else {
                if (x->children[i]->num_keys >= t) {
                    BNode<T, t>* c = x->children[i];
                    removeInternal(c, key);
                }
                // case 3a 3b: x.ci has only t - 1 keys...
                else {
                    BNode<T, t>* c = x->children[i];                 // center child
                    BNode<T, t>* y = i > 0 ? x->children[i - 1] : nullptr;  // left sibling
                    BNode<T, t>* z = i < x->num_keys ? x->children[i + 1] : nullptr; // right sibling

                    // case 3a: z (right sibling) has at least t keys
                    if (z && z->num_keys >= t) {
                        c->keys[c->num_keys] = x->keys[i];
                        c->children[c->num_keys + 1] = z->children[0];
                        ++c->num_keys;
                        x->keys[i] = z->keys[0];
                        std::memmove(z->keys, &z->keys[1], (z->num_keys - 1) * sizeof(T));
                        if (!z->leaf) {
                            std::memmove(z->children, &z->children[1], z->num_keys * sizeof(BNode<T, t>*));
                        }
                        --z->num_keys;
                        removeInternal(c, key);
                    }
                    // case 3a: y (left sibling) has at least t keys
                    else if (y && y->num_keys >= t) {
                        std::memmove(&c->keys[1], &c->keys[0], c->num_keys * sizeof(T));
                        if (!c->leaf) {
                            std::memmove(&c->children[1], &c->children[0], (c->num_keys + 1) * sizeof(BNode<T, t>*));
                        }
                        c->keys[0] = x->keys[i - 1];
                        c->children[0] = y->children[y->num_keys];
                        ++c->num_keys;
                        x->keys[i - 1] = y->keys[y->num_keys - 1];
                        --y->num_keys;
                        removeInternal(c, key);
                    }
                    // case 3b: siblings have t - 1 keys
                    else {
                        if (z) {
                            c->keys[t - 1] = x->keys[i];
                            std::memcpy(&c->keys[t], &z->keys[0], (t - 1) * sizeof(T));
                            if (!c->leaf) {
                                std::memcpy(&c->children[t], &z->children[0], t * sizeof(BNode<T, t>*));
                            }
                            
                            c->num_keys = 2 * t - 1;
                            std::memmove(&x->keys[i], &x->keys[i + 1], (x->num_keys - i - 1) * sizeof(T));
                            std::memmove(&x->children[i + 1], &x->children[i + 2], (x->num_keys - i - 1) * sizeof(BNode<T, t>*));
                            
                            --x->num_keys;
                            delete z;
                            removeInternal(c, key);
                        }
                        else {
                            y->keys[t - 1] = x->keys[i - 1];
                            std::memcpy(&y->keys[t], &c->keys[0], (t - 1) * sizeof(T));
                            if (!y->leaf) {
                                std::memcpy(&y->children[t], &c->children[0], t * sizeof(BNode<T, t>*));
                            }
                            y->num_keys = 2 * t - 1;
                            --x->num_keys;
                            delete c;
                            removeInternal(y, key);
                        }
                    }
                }
            }
        }
    }

    void traverse(BNode<T, t>* x) const {
        int i;
        for (i = 0; i < x->num_keys; i++) {
            if (!x->leaf) traverse(x->children[i]);
            std::cout << x->keys[i] << " ";
        }
        if (!x->leaf) traverse(x->children[i]);
    }
    

public:
    BTree() : root(nullptr) {}

    std::pair<BNode<T, t>*, size_t> search(BNode<T, t>* x, T key) {
        if (!x) return {nullptr, 0};
        size_t i = binarySearch(x, key);
        if (key == x->keys[i]) {
            return {x, i};
        }
        else if (x->leaf) {
            return nullptr;
        }
        else { // diskRead(x.ci)
            return search(x->children[i], key);
        }
    }

    void insert(T key) {
        if (!root) {
            root = new BNode<T, t>(true);
            root->keys[0] = key;
            root->num_keys = 1;
            return;
        }
        if (root->num_keys == 2 * t - 1) {
            BNode<T, t>* s = splitRoot(root);
            insertNonFull(s, key);
        }
        else {
            insertNonFull(root, key);
        }
    }

    bool remove(T key) {
        if (!root) return false;
        removeInternal(root, key);
        // if root have 0 children, then lower the height
        if (root->num_keys == 0) {
            BNode<T, t>* tmp = root;
            if (root->leaf) {
                root = nullptr;
            } else {
                root = root->children[0];
            }
            delete tmp;
        }
        return true;
    }

    void print() const {
        if (root != nullptr) traverse(root);
        std::cout << std::endl;
    }

};

#endif
