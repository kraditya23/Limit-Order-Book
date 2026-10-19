#include "lob/book.hpp"
#include <algorithm>
#include <limits>
#include <cassert>

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

Status add_order (OrderBook& book, const OrderRequest& req, std::vector<Trade>& trades) {
    // invalid requests
    if (req.qty == 0) return Status::InvalidQty;
    if (book.order_map.contains(req.id)) return Status::DuplicateId;

    // mutate : from here onwards the book changes
    auto& level_map = (req.side == Side::Buy) ? book.bid_level_map : book.ask_level_map;

    Quantity remaining = sweep(book, req.side, req.id, req.price, req.qty, req.ts, trades);
    if (remaining == 0) return Status::Ok;

    // allocate and fill order
    Order* order = book.order_pool.allocate();
    order->id = req.id;
    order->timestamp = req.ts;
    order->side = req.side;
    order->price = req.price;
    order->initial_qty = req.qty;
    order->remaining_qty = remaining;

    // find or create the price level
    PriceLevel* level = nullptr;
    auto it = level_map.find(req.price);
    if (it != level_map.end()) {
        level = it->second;
    } else {
        level = book.level_pool.allocate();
        level->price = req.price;
        PriceLevel*& tree_root = (req.side == Side::Buy) ? book.bid_tree_root : book.ask_tree_root;
        insert_level(tree_root, level, level_map);
    }

    // append to tail of fifo sequence
    order->parent_level = level;
    order->prev = level->tail_order;
    order->next = nullptr;

    if (level->tail_order) level->tail_order->next = order;
    else level->head_order = order;

    level->tail_order = order;

    // update level stats
    level->total_volume += remaining;
    level->order_count++;

    // register in order map
    book.order_map[req.id] = order;

    return Status::Ok;
}

Status cancel_order (OrderBook& book, OrderId id){
    // 1. O(1) lookup
    auto it = book.order_map.find(id);
    if (it == book.order_map.end()) return Status::UnknownId;
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

    return Status::Ok;
}

// modify order flow:
// new_qty == 0                             -> cancel only
// new_price != old_price                   -> cancel + re-add
// new_qty > remaining_qty                  -> cancel + re-add
// new_qty < remaining_qty (same price)     -> in place reduction
// new_qty == remaining_qty (same price)    -> no operation
Status modify_order (OrderBook& book, const ModifyRequest& req, std::vector<Trade>& trades) {
    auto it = book.order_map.find(req.id);
    if (it == book.order_map.end()) return Status::UnknownId;
    Order* order = it->second;

    // case 1: new_qty == 0  -> cancel
    if (req.new_qty == 0) {
        return cancel_order(book, req.id);
    }

    // case 2: new_price != old_price     -> cancel + re-add
    // or case 3: new_qty > remaining_qty  -> cancel + re-add
    if (req.new_price != order->price || req.new_qty > order->remaining_qty) {
        Side side = order->side;    // saving before cancel order frees the order
        [[maybe_unused]] Status cancelled = cancel_order(book, req.id);  
        assert(cancelled == Status::Ok);    // id was found above -> cannot fail

        Status added = add_order(book, {.id = req.id, .side = side, .price = req.new_price, .qty = req.new_qty, .ts = req.ts}, trades);
        assert(added == Status::Ok);    // qty > 0 and id was just freed -> cannot be rejected

        return added;
    }

    // case 4: new_qty < remaining_qty (same price) -> in place reduction
    if (req.new_qty < order->remaining_qty) {
        Quantity delta = order->remaining_qty - req.new_qty;
        order->remaining_qty = req.new_qty;
        order->parent_level->total_volume -= delta;
    }

    // case 5: new_qty == remaining_qty (same price) -> do nothing

    return Status::Ok;
}

Status market_order (OrderBook& book, const OrderRequest& req, std::vector<Trade>& trades) {
    // rejecting invalid orders
    if (req.qty == 0) return Status::InvalidQty;
    if (book.order_map.contains(req.id)) return Status::DuplicateId;

    Price limit = (req.side == Side::Buy) ? std::numeric_limits<Price>::max() 
                                      : std::numeric_limits<Price>::min();

    sweep(book, req.side, req.id, limit, req.qty, req.ts, trades);
    return Status::Ok;
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