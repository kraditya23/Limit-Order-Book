#include "tests/standalone_avl.hpp"

namespace lob {

void StandaloneAVL::rotate_left(AVLNode* x) {
    AVLNode* y = x->right;
    x->right = y->left;

    if (y->left != nullptr) {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if (x->parent == nullptr) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    x->parent = y;
    y->left = x;

    // updating the heights (bottom first then top)
    update_height(x);
    update_height(y);
}

void StandaloneAVL::rotate_right(AVLNode* y) {
    AVLNode* x = y->left;
    y->left = x->right;

    if (x->right != nullptr) {
        x->right->parent = y;
    }

    x->parent = y->parent;

    if (y->parent == nullptr) {
        root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }

    y->parent = x;
    x->right = y;

    update_height(y);
    update_height(x);
}

void StandaloneAVL::rebalance(AVLNode* node) {
    if (!node) return;

    int balance = get_balance(node);

    // left heavy
    if (balance > 1) {
        if (get_balance(node->left) < 0) {
            rotate_left(node->left);
        }
        rotate_right(node);
    }
    // right heavy
    else if (balance < -1) {
        if (get_balance(node->right) > 0) {
            rotate_right(node->right);
        } 
        rotate_left(node);
    }
}

void StandaloneAVL::insert(int key) {
    if (root == nullptr) {
        root = new AVLNode(key);
        return;
    }

    AVLNode* curr = root;
    AVLNode* parent = nullptr;

    while (curr != nullptr) {
        parent = curr;
        if (key < curr->key) {
            curr = curr->left;
        } else if (key > curr->key) {
            curr = curr->right;
        } else {
            return;
        }
    }

    AVLNode* new_node = new AVLNode(key);
    new_node->parent = parent;

    if (parent->key > key) {
        parent->left = new_node;
    } else {
        parent->right = new_node;
    }

    curr = new_node;

    while (curr != nullptr) {
        update_height(curr);
        rebalance(curr);
        curr = curr->parent;
    }
}

AVLNode* StandaloneAVL::find_min() const {
    AVLNode* curr = root;
    if (!curr) return nullptr;
    while (curr->left != nullptr) {
        curr = curr->left;
    }
    return curr;
}

AVLNode* StandaloneAVL::find_max() const {
    AVLNode* curr = root;
    if (!curr) return nullptr;
    while (curr->right != nullptr) {
        curr = curr->right;
    }
    return curr;
}

void StandaloneAVL::remove(int key) {
    AVLNode* curr = root;
    //1. top down search to find the node
    while (curr != nullptr && curr->key != key) {
        if (key < curr->key) curr = curr->left;
        else curr = curr->right;
    }

    if (curr == nullptr) return; // key not found

    AVLNode* node_to_delete = curr;
    AVLNode* start_rebalance_node = nullptr;

    if (node_to_delete->left != nullptr && node_to_delete->right != nullptr) {
        AVLNode* successor = node_to_delete->right;
        while (successor->left != nullptr) {
            successor = successor->left;
        }

        node_to_delete->key = successor->key;
        node_to_delete = successor;
    }

    AVLNode* child = (node_to_delete->left != nullptr) ? node_to_delete->left : node_to_delete->right;

    if (child != nullptr) {
        child->parent = node_to_delete->parent;
    }

    if (node_to_delete->parent == nullptr) {
        root = child;
        start_rebalance_node = nullptr;
    } else {
        if (node_to_delete == node_to_delete->parent->left) {
            node_to_delete->parent->left = child;
        } else {
            node_to_delete->parent->right = child;
        }
        start_rebalance_node = node_to_delete->parent;
    }

    delete node_to_delete;

    curr = start_rebalance_node;
    while (curr != nullptr) {
        update_height(curr);
        rebalance(curr);
        curr = curr->parent;
    }
}

}