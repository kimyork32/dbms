#ifndef BTREE_H
#define BTREE_H

#include <iostream>

// t = minimum degree (min t=2)
template <typename T, int t = 2>
struct BNode {
    bool is_leaf;
    int num_keys;
    T keys[2 * t - 1];       // max keys: 2t - 1
    BNode* children[2 * t];  // max children: 2t

    BNode(bool leaf = true) {
        is_leaf = leaf;
        num_keys = 0;
        for (int i = 0; i < 2 * t; i++) children[i] = nullptr;
    }
};

template <typename T, int t = 2>
class BTree {
private:
    BNode<T, t>* root;

    void splitChild(BNode<T, t>* x, int i, BNode<T, t>* y) {
        BNode<T, t>* z = new BNode<T, t>(y->is_leaf);
        z->num_keys = t - 1; // z takes the keys to the right of the median

        // move the top t-1 keys from y to z
        for (int j = 0; j < t - 1; j++) {
            z->keys[j] = y->keys[j + t];
        }

        // if it is not a leaf, move the corresponding t children to z
        if (!y->is_leaf) {
            for (int j = 0; j < t; j++) {
                z->children[j] = y->children[j + t];
            }
        }

        y->num_keys = t - 1; // y keeps the keys to the left of the median

        // shift children of x to accommodate z
        for (int j = x->num_keys; j >= i + 1; j--) {
            x->children[j + 1] = x->children[j];
        }
        x->children[i + 1] = z;

        // shift keys of x to move up the median of y
        for (int j = x->num_keys - 1; j >= i; j--) {
            x->keys[j + 1] = x->keys[j];
        }
        x->keys[i] = y->keys[t - 1]; // median key
        x->num_keys++;
    }

    void insertNonFull(BNode<T, t>* x, T k) {
        int i = x->num_keys - 1;
        
        if (x->is_leaf) {
            while (i >= 0 && x->keys[i] > k) {
                x->keys[i + 1] = x->keys[i];
                i--;
            }
            x->keys[i + 1] = k;
            x->num_keys++;
        } else {
            while (i >= 0 && x->keys[i] > k) i--;
            i++;
            
            if (x->children[i]->num_keys == 2 * t - 1) { // check for maximum filling
                splitChild(x, i, x->children[i]);
                if (x->keys[i] < k) i++;
            }
            insertNonFull(x->children[i], k);
        }
    }

    void traverse(BNode<T, t>* x) const {
        int i;
        for (i = 0; i < x->num_keys; i++) {
            if (!x->is_leaf) traverse(x->children[i]);
            std::cout << x->keys[i] << " ";
        }
        if (!x->is_leaf) traverse(x->children[i]);
    }

public:
    BTree() : root(nullptr) {}

    void insert(T k) {
        if (root == nullptr) {
            root = new BNode<T, t>(true);
            root->keys[0] = k;
            root->num_keys = 1;
        } else {
            if (root->num_keys == 2 * t - 1) {
                BNode<T, t>* s = new BNode<T, t>(false);
                s->children[0] = root;
                splitChild(s, 0, root);
                
                int i = 0;
                if (s->keys[0] < k) i++;
                insertNonFull(s->children[i], k);
                root = s;
            } else {
                insertNonFull(root, k);
            }
        }
    }

    void print() const {
        if (root != nullptr) traverse(root);
        std::cout << std::endl;
    }
};

#endif // BTREE_H
