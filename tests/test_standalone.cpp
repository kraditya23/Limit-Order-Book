#include "standalone_avl.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <random>
#include <vector>

using namespace lob;

namespace {

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

}// namespace 

TEST(StandaloneAvl, ShuffledInsertThenDeleteKeepsInvariants) {
    StandaloneAVL tree;
    std::vector<int> keys;
    for (int i = 0; i < 1000; i++) keys.push_back(i);

    std::mt19937 g(42);

    std::shuffle(keys.begin(), keys.end(), g);
    for (int key : keys) {
        tree.insert(key);
        ASSERT_TRUE(validate_avl_invariants(tree.get_root())) << "after inserting " << key;
    }

    std::shuffle(keys.begin(), keys.end(), g);
    for (int key : keys) {
        tree.remove(key);
        ASSERT_TRUE(validate_avl_invariants(tree.get_root())) << "after removing " << key;
    }

    EXPECT_EQ(tree.get_root(), nullptr);
}