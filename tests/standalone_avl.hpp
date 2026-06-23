#pragma once
#include<algorithm>
#include<iostream>

namespace lob {

struct AVLNode {
    int key;
    int height = 1;
    AVLNode* left = nullptr;
    AVLNode* right = nullptr;
    AVLNode* parent = nullptr;

    explicit AVLNode(int val) : key(val) {}
};

class StandaloneAVL {
private:
    AVLNode* root = nullptr;

    void destroy(AVLNode *node) {
        if (!node) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    int get_height (AVLNode *node) const {
        return node ? node->height : 0;
    }

    int get_balance (AVLNode* node) const {
        return node ? get_height(node->left) - get_height(node->right) : 0;
    }

    void update_height (AVLNode *node) {
        if (node) {
            node->height = 1 + std::max(get_height(node->left), get_height(node->right));
        }
    }

    void rotate_left(AVLNode *x);
    void rotate_right(AVLNode *y);
    void rebalance(AVLNode *node);

public:
    StandaloneAVL() = default;

    ~StandaloneAVL() {
        destroy(root);
    }

    void insert(int key);
    void remove(int key);

    AVLNode* find_min() const;
    AVLNode* find_max() const;
    AVLNode* get_root() const {return root;}
};

} // namespace lob