#include "lob/book.hpp"
#include <algorithm>

namespace lob {
namespace { // <- anonymous namespace (makes these helpers file local)

int get_height (const PriceLevel* n) {
    return n ? n->height : 0;
}

int get_balance (const PriceLevel* n) {
    return n ? get_height(n->left) - get_height(n->right) : 0;
}

void update_height (PriceLevel* n) {
    if (n) n->height = 1 + std::max(get_height(n->left), get_height(n->right));
}

void rotate_left (PriceLevel*& tree_root, PriceLevel* x) {
    PriceLevel* y = x->right;
    x->right = y->left;

    if (y->left) y->left->parent = x;

    y->parent = x->parent;

    if (!x->parent) tree_root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;

    y->left = x;
    x->parent = y;

    update_height(x);
    update_height(y);
}

void rotate_right (PriceLevel*& tree_root, PriceLevel* y) {
    PriceLevel* x = y->left;
    y->left = x->right;
    if (x->right) x->right->parent = y;

    x->parent = y->parent;

    if (!y->parent) tree_root = x;
    else if (y->parent->left == y) y->parent->left = x;
    else y->parent->right = x;

    x->right = y;
    y->parent = x;

    update_height(y);
    update_height(x);
}

void rebalance (PriceLevel*& tree_root, PriceLevel* node) {
    int balance = get_balance(node);

    if (balance > 1) {
        if (get_balance(node->left) < 0) {
            rotate_left(tree_root, node->left);
        }
        rotate_right(tree_root, node);
    } else if (balance < -1) {
        if (get_balance(node->right) > 0) {
            rotate_right(tree_root, node->right);
        }
        rotate_left(tree_root, node);
    }
}

void insert_level (OrderBook& book, PriceLevel*& tree_root, PriceLevel* level) {
    // BST walk to find the insertion point
    PriceLevel* cur = tree_root;
    PriceLevel* parent = nullptr;
    while (cur != nullptr) {
        parent = cur;
        if (level->price < cur->price) cur = cur->left;
        else cur = cur->right;
    }

    level->parent = parent;
    if (!parent) tree_root = level;
    else if (level->price < parent->price) parent->left = level;
    else parent->right = level;

    cur = level;
    while (cur != nullptr) {
        update_height(cur);
        rebalance(tree_root, cur);
        cur = cur->parent;
    }

    book.level_map[level->price] = level;
}

void remove_level (OrderBook& book, PriceLevel*& tree_root, PriceLevel* target) {
    // node = the node physically unlinked from the tree structure
    PriceLevel* node;
    if (!target->left || !target->right) {
        node = target;
    } else {
        node = target->right;
        while (node->left) node = node->left;
    }

    // node has at most one child (right only)
    PriceLevel* child = node->left ? node->left : node->right;

    // splice the node out
    if (child) child->parent = node->parent;

    PriceLevel* start_rebalance;
    if (!node->parent) {
        tree_root = child;
        start_rebalance = nullptr;
    } else {
        if (node == node->parent->left) node->parent->left = child;
        else node->parent->right = child;
        start_rebalance = node->parent;
    }

    // two children case: move successor into target's structural position
    if (node != target) {
        if (start_rebalance == target) start_rebalance = node; // since target will be deleted and replaced by node

        node->parent = target->parent;
        node->left = target->left;
        node->right = target->right;

        if (!target->parent) tree_root = node;
        else if (target == target->parent->left) target->parent->left = node;
        else target->parent->right = node;

        if (target->left) target->left->parent = node;
        if (target->right) target->right->parent = node;
    }

    // rebalance upward from where the physical removal happened
    PriceLevel* cur = start_rebalance;
    while (cur) {
        update_height(cur);
        rebalance(tree_root, cur);
        cur = cur->parent;
    }

    book.level_map.erase(target->price);
    book.level_pool.deallocate(target);
}

} // anonymous namespace

void add_order (OrderBook& book, OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    // 1. allocate and fill order
    Order* order = book.order_pool.allocate();
    order->id = id;
    order->timestamp = ts;
    order->side = side;
    order->price = price;
    order->initial_qty = qty;
    order->remaining_qty = qty;

    // 2. find or create the price level
    PriceLevel* level = nullptr;
    auto it = book.level_map.find(price);
    if (it != book.level_map.end()) {
        level = it->second;
    } else {
        level = book.level_pool.allocate();
        level->price = price;
        PriceLevel*& tree_root = (side == Side::Buy) ? book.bid_tree_root : book.ask_tree_root;
        insert_level(book, tree_root, level);
    }

    // 3. append to tail of fifo sequence
    order->parent_level = level;
    order->prev = level->tail_order;
    order->next = nullptr;

    if (level->tail_order) level->tail_order->next = order;
    else level->head_order = order;

    level->tail_order = order;

    // 4. update level stats
    level->total_volume += qty;
    level->order_count++;

    // 5. register in order map
    book.order_map[id] = order;
}

void cancel_order (OrderBook& book, OrderId id){
    // 1. O(1) lookup
    auto it = book.order_map.find(id);
    if (it == book.order_map.end()) return;
    Order* order = it->second;
    PriceLevel* level = order->parent_level;

    // 2. splice out the FIFO list
    if (order->prev) order->prev->next = order->next;
    else level->head_order = order->next;

    if (order->next) order->next->prev = order->prev;
    else level->tail_order = order->prev;

    // 3. update the level stats
    level->total_volume -= order->remaining_qty;
    level->order_count--;

    // 4. if level is now empty, remove it from the tree and map
    if (level->order_count == 0) {
        PriceLevel*& tree_root = (order->side == Side::Buy) ? book.bid_tree_root : book.ask_tree_root;
        remove_level(book, tree_root, level);
    }

    // 5. free the order
    book.order_map.erase(it);
    book.order_pool.deallocate(order);
}

} // namespace lob