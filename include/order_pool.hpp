// include/order_pool.hpp
#pragma once
#include "order.hpp"
#include <vector>
#include <stack>
#include <atomic>

class OrderPool {
public:
    // ИСПРАВЛЕНИЕ: Убрали free_indices_(capacity) из списка инициализации
    explicit OrderPool(size_t capacity) : orders_(capacity) {
        // std::stack будет увеличиваться по мере вызова push
        for (size_t i = 0; i < capacity; ++i) {
            free_indices_.push(i);
        }
        next_id_.store(1);
    }

    Order* acquire(int qty, int32_t price_cents) {
        if (free_indices_.empty()) return nullptr;
        size_t idx = free_indices_.top();
        free_indices_.pop();
        uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        orders_[idx].id = id;
        orders_[idx].price_cents = price_cents;
        orders_[idx].quantity = qty;
        orders_[idx].active = true;
        return &orders_[idx];
    }

    void release(Order* order) {
        if (!order || !order->active) return;

        // Проверка: order лежит внутри [orders_.data(), orders_.data() + orders_.size())
        Order* begin = orders_.data();
        Order* end = begin + orders_.size();
        if (order < begin || order >= end) {
            // Это не наш ордер! Кто-то использовал new/delete напрямую.
            // Лучше упасть явно, чем повредить память.
            std::cerr << "FATAL: release() called with foreign pointer: " << order << "\n";
            std::cerr << "Pool range: [" << begin << ", " << end << ")\n";  
            std::abort();
        }

        order->active = false;
        size_t idx = order - begin;
        free_indices_.push(idx);
    }

    // Для отладки
    size_t available() const { return free_indices_.size(); }

private:
    std::vector<Order> orders_;
    std::stack<size_t> free_indices_;
    std::atomic<uint64_t> next_id_{1};
};