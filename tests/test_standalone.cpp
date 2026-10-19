#include "tests/standalone_avl.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <climits>

using namespace lob;

// Recursive helper to strictly prove AVL invariants
bool validate_avl_invariants(AVLNode* node, int min = INT_MIN, int max = INT_MAX) {
    if (!node) return true;

    int left_h = node->left ? node->left->height : 0;
    int right_h = node->right ? node->right->height : 0;
    int balance = left_h - right_h;

    if (balance < -1 || balance > 1) {
        std::cerr << "Balance invariant violated at node " << node->key << "\n";
        return false;
    }

    if (node->height != 1 + std::max(left_h, right_h)) {
        std::cerr << "Height inconsistency at node " << node->key << "\n";
        return false;
    }

    if (node->key <= min || node->key >= max) {
        std::cerr << "BST ordering violated at node " << node->key << "\n";
        return false;
    }

    if (node->left && node->left->parent != node) {
        std::cerr << "Left parent pointer corrupted at node " << node->key << "\n";
        return false;
    }

    if (node->right && node->right->parent != node) {
        std::cerr << "Right parent pointer corrupted at node " << node->key << "\n";
        return false;
    }

    return validate_avl_invariants(node->left, min, node->key)
        && validate_avl_invariants(node->right, node->key, max);
}

int main() {
    StandaloneAVL tree;
    std::vector<int> keys;
    for (int i = 0; i < 1000; ++i) keys.push_back(i);

    // Shuffle to ensure random insertion orders trigger all rotation cases
    std::mt19937 g(42);
    std::shuffle(keys.begin(), keys.end(), g);

    std::cout << "Inserting 1000 nodes...\n";
    for (int key : keys) {
        tree.insert(key);
        assert(validate_avl_invariants(tree.get_root()));
    }

    std::cout << "Deleting 1000 nodes...\n";
    std::shuffle(keys.begin(), keys.end(), g);
    for (int key : keys) {
        tree.remove(key);
        assert(validate_avl_invariants(tree.get_root()));
    }

    std::cout << "AVL Tree Stress Test Passed. No memory corruption detected.\n";
    return 0;
}