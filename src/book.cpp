#include "lob/book.hpp"
#include <algorithm>
#include <limits>

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

void insert_level (PriceLevel*& tree_root, PriceLevel* level, std::unordered_map<Price, PriceLevel*>& level_map) {
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

    level_map[level->price] = level;
}

void remove_level (OrderBook& book, PriceLevel*& tree_root, PriceLevel* target, std::unordered_map<Price, PriceLevel*>& level_map) {
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

    level_map.erase(target->price);
    book.level_pool.deallocate(target);
}

PriceLevel* find_min (PriceLevel* root) {
    if (!root) return nullptr;
    while (root->left) root = root->left;
    return root;
}

PriceLevel* find_max(PriceLevel* root) {
    if (!root) return nullptr;
    while (root->right) root = root->right;
    return root;
}

Quantity sweep (OrderBook& book, Side aggressor_side, OrderId taker_id, Price limit_price,
                Quantity qty, Timestamp ts, std::vector<Trade>& trades) {
    PriceLevel*& opp_tree = (aggressor_side == Side::Buy)
                            ? book.ask_tree_root : book.bid_tree_root;
    auto& opp_map = (aggressor_side == Side::Buy) 
                    ? book.ask_level_map : book.bid_level_map;
    
    while (qty > 0) {
        // best ask for market buy (find_min), best bid for market sell (find_max)
        PriceLevel* level = (aggressor_side == Side::Buy) ? find_min(opp_tree) : find_max(opp_tree);

        if (!level) break; // book exhausted

        // limit price check (always passes for market orders)
        if (aggressor_side == Side::Buy && level->price > limit_price) break;
        if (aggressor_side == Side::Sell && level->price < limit_price) break;

        // fill fifo at this level
        Order* maker = level->head_order;
        while (maker && qty > 0) {
            Quantity fill = std::min(qty, maker->remaining_qty);
            trades.push_back({maker->id, taker_id, level->price, fill, ts});
            maker->remaining_qty -= fill;
            level->total_volume -= fill;
            qty -= fill;

            if (maker->remaining_qty == 0) {
                Order* next = maker->next;
                level->head_order = next;
                if (next) next->prev = nullptr;
                else level->tail_order = nullptr;

                level->order_count--;
                book.order_map.erase(maker->id);
                book.order_pool.deallocate(maker);
                maker = next;
            }
        }

        // if level is drained, remove it from the tree + map
        if (level->order_count == 0) {
            remove_level(book, opp_tree, level, opp_map);
        }
    }
    return qty;
}

} // anonymous namespace

std::vector<Trade> add_order (OrderBook& book, OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    auto& level_map = (side == Side::Buy) ? book.bid_level_map : book.ask_level_map;

    std::vector<Trade> trades;
    Quantity remaining = sweep(book, side, id, price, qty, ts, trades);
    if (remaining == 0) return trades;

    // 1. allocate and fill order
    Order* order = book.order_pool.allocate();
    order->id = id;
    order->timestamp = ts;
    order->side = side;
    order->price = price;
    order->initial_qty = qty;
    order->remaining_qty = remaining;

    // 2. find or create the price level
    PriceLevel* level = nullptr;
    auto it = level_map.find(price);
    if (it != level_map.end()) {
        level = it->second;
    } else {
        level = book.level_pool.allocate();
        level->price = price;
        PriceLevel*& tree_root = (side == Side::Buy) ? book.bid_tree_root : book.ask_tree_root;
        insert_level(tree_root, level, level_map);
    }

    // 3. append to tail of fifo sequence
    order->parent_level = level;
    order->prev = level->tail_order;
    order->next = nullptr;

    if (level->tail_order) level->tail_order->next = order;
    else level->head_order = order;

    level->tail_order = order;

    // 4. update level stats
    level->total_volume += remaining;
    level->order_count++;

    // 5. register in order map
    book.order_map[id] = order;

    return trades;
}

void cancel_order (OrderBook& book, OrderId id){
    // 1. O(1) lookup
    auto it = book.order_map.find(id);
    if (it == book.order_map.end()) return;
    Order* order = it->second;
    PriceLevel* level = order->parent_level;
    Side side = order->side;

    auto& level_map = (side == Side::Buy) ? book.bid_level_map : book.ask_level_map;

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
        remove_level(book, tree_root, level, level_map);
    }

    // 5. free the order
    book.order_map.erase(it);
    book.order_pool.deallocate(order);
}

// modify order flow:
// new_qty == 0                             -> cancel only
// new_price != old_price                   -> cancel + re-add
// new_qty > remaining_qty                  -> cancel + re-add
// new_qty == remaining_qty (same price)    -> no operation
// new_qty < remaining_qty (same price)     -> in place reduction
std::vector<Trade> modify_order (OrderBook& book, OrderId id, Price new_price, Quantity new_qty, Timestamp ts) {
    auto it = book.order_map.find(id);
    if (it == book.order_map.end()) return {};
    Order* order = it->second;

    // case 1: new_qty == 0  -> cancel
    if (new_qty == 0) {
        cancel_order(book, id);
        return {};
    }

    // case 2: new_price != old_price     -> cancel + re-add
    // or case 3: new_qty > remainig_qty  -> cancel + re-add
    if (new_price != order->price || new_qty > order->remaining_qty) {
        Side side = order->side;    // saving before cancel order frees the order
        cancel_order(book, id);  
        return add_order(book, id, side, new_price, new_qty, ts);
    }

    // case 4: new_qty == remaining_qty (same price) -> do nothing

    // case 5: new_qty < remaining_qty (same price) -> in place reduction
    if (new_qty < order->remaining_qty) {
        Quantity delta = order->remaining_qty - new_qty;
        order->remaining_qty = new_qty;
        order->parent_level->total_volume -= delta;
        return {};
    }

    return {};
}

std::vector<Trade> market_order (OrderBook& book, OrderId id, Side side, Quantity qty, Timestamp ts) {
    std::vector<Trade> trades;

    Price limit = (side == Side::Buy) ? std::numeric_limits<Price>::max() 
                                      : std::numeric_limits<Price>::min();

    sweep(book, side, id, limit, qty, ts, trades);
    return trades;
}

Price best_bid (const OrderBook& book) {
    PriceLevel* level = find_max(book.bid_tree_root);
    return level ? level->price : std::numeric_limits<Price>::min();
}

Price best_ask (const OrderBook& book) {
    PriceLevel* level = find_min(book.ask_tree_root);
    return level ? level->price : std::numeric_limits<Price>::max();
}

} // namespace lob