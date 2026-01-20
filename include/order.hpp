/**
 * @file order.hpp
 * @brief This file contains the declaration of the Order class.
 * 
 * The Order class represents an order in an order book. It stores information such as the quantity, price, side, and timestamp of the order.
 * The class provides setters and getters to modify and access the order's properties.
 */

#pragma once

#include <cstdint>
#include "enums.hpp"
#include "helpers.hpp"

inline uint64_t generate_unique_id() {
    static uint64_t s_next_id{0};
    return ++s_next_id;
}

struct Order {
    uint64_t id;
    int32_t price_cents;
    int quantity;
    bool active = false; // помечает, используется ли слот

    Order() = default;
    Order(uint64_t id_, int qty, int32_t price, bool active_ = true)
        : id(id_), price_cents(price), quantity(qty), active(active_) {}
};
