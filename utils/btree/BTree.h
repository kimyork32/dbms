#ifndef BTREE_H_INCLUDED
#define BTREE_H_INCLUDED

#include <iostream>

template <typename T, int M = 3>
struct BNode {
    bool is_leaf;
    int num_keys;
    T keys[M - 1];
    BNode* children[M];

    BNode(bool leaf = true) {
        is_leaf = leaf;
        num_keys = 0;
        for (int i = 0; i < M; i++) children[i] = nullptr;
    }
};

template <typename T, int M = 3>
class BTree {
private:
    BNode<T, M>* root;

    void splitChild(BNode<T, M>* x, int i, BNode<T, M>* y) {
        BNode<T, M>* z = new BNode<T, M>(y->is_leaf);
        int mid = (M - 1) / 2;
        int t = (M + 1) / 2;

        z->num_keys = (M - 1) - t;
        for (int j = 0; j < z->num_keys; j++) {
            z->keys[j] = y->keys[j + t];
        }

        if (!y->is_leaf) {
            for (int j = 0; j < t; j++) {
                z->children[j] = y->children[j + t];
            }
        }

        y->num_keys = t - 1;

        for (int j = x->num_keys; j >= i + 1; j--) {
            x->children[j + 1] = x->children[j];
        }
        x->children[i + 1] = z;

        for (int j = x->num_keys - 1; j >= i; j--) {
            x->keys[j + 1] = x->keys[j];
        }
        x->keys[i] = y->keys[t - 1];
        x->num_keys++;
    }

    void insertNonFull(BNode<T, M>* x, T k) {
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
            if (x->children[i]->num_keys == M - 1) {
                splitChild(x, i, x->children[i]);
                if (x->keys[i] < k) i++;
            }
            insertNonFull(x->children[i], k);
        }
    }

    void traverse(BNode<T, M>* x) {
        int i;
        for (i = 0; i < x->num_keys; i++) {
            if (!x->is_leaf) traverse(x->children[i]);
            std::cout << " " << x->keys[i];
        }
        if (!x->is_leaf) traverse(x->children[i]);
    }

public:
    BTree() { root = nullptr; }

    void insert(T k) {
        if (root == nullptr) {
            root = new BNode<T, M>(true);
            root->keys[0] = k;
            root->num_keys = 1;
        } else {
            if (root->num_keys == M - 1) {
                BNode<T, M>* s = new BNode<T, M>(false);
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

    void print() {
        if (root != nullptr) traverse(root);
        std::cout << std::endl;
    }
};

#endif // BTREE_H_INCLUDED
