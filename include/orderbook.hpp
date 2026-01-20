/**
 * @file orderbook.hpp
 * @brief This file contains the declaration of the Orderbook class.
 * 
 * The Orderbook class represents an order book, which is a collection of buy and sell orders.
 * It provides functionality to add orders, execute orders, and retrieve the best quote.
 * The order book is implemented using two maps, one for buy orders (bids) and one for sell orders (asks).
 * Each map is sorted based on the price of the orders.
 * The Orderbook class also provides methods to clean up empty keys and print the order book.
 */

#pragma once

#include <deque>
#include <map>
#include <unordered_map>
#include <memory>
#include "enums.hpp"
#include "order.hpp"
#include "order_pool.hpp"
#include <set>

static const int MIN_PRICE_CENTS = 1;
static const int MAX_PRICE_CENTS =  200000; // $2000.00 — достаточно для $1500
static const int PRICE_RANGE = MAX_PRICE_CENTS - MIN_PRICE_CENTS + 1; // 100000

struct PriceLevel {
    std::vector<Order*> orders;
    size_t head = 0;

    // Типы итераторов
    using iterator = typename std::vector<Order*>::iterator;
    using const_iterator = typename std::vector<Order*>::const_iterator;

    // Начало "живых" ордеров
    iterator begin() {
        return orders.begin() + head;
    }

    const_iterator begin() const {
        return orders.begin() + head;
    }

    // Конец всех ордеров (включая "мертвые")
    iterator end() {
        return orders.end();
    }

    const_iterator end() const {
        return orders.end();
    }

    // Удаление по итератору
    iterator erase(iterator pos) {
        if (pos < begin()) {
            // Это "мертвый" ордер — не удаляем
            return pos;
        }
        return orders.erase(pos);
    }

    // Удаление по индексу (если нужно)
    void erase(size_t index) {
        if (index < head) return; // мёртвый
        orders.erase(orders.begin() + index);
    }

    // Остальные методы (как раньше)
    bool empty() const { return head >= orders.size(); }
    // Неконстантная версия — для изменения
    Order*& front() {
        if (empty()) {
            static Order* null_ptr = nullptr;
            return null_ptr;
        }
        return orders[head];
    }

    // Константная версия — для чтения
    const Order* front() const {
        if (empty()) return nullptr;
        return orders[head];
    }
    void pop_front() { if (!empty()) head++; }
    void push_back(Order* order) { orders.push_back(order); }
    size_t size() const { return orders.size() - head; }

    // Опционально: compact()
    void compact() {
        if (head == 0) return;
        if (head >= orders.size()) {
            orders.clear();
            head = 0;
            return;
        }
        std::move(orders.begin() + head, orders.end(), orders.begin());
        orders.resize(orders.size() - head);
        head = 0;
    }

    // Неконстантный доступ
Order*& operator[](size_t index) {
    return orders[head + index];
}

// Константный доступ
const Order* operator[](size_t index) const {
    return orders[head + index];
}
};


class Orderbook {
private:
    // std::map<double, std::deque<std::unique_ptr<Order>>, std::greater<double>> m_bids;
    // std::map<double, std::deque<std::unique_ptr<Order>>, std::less<double>> m_asks;
    // std::vector<std::deque<std::unique_ptr<Order>>> m_bids;
    // std::vector<std::deque<std::unique_ptr<Order>>> m_asks;

    std::vector<PriceLevel> m_bids;
    std::vector<PriceLevel> m_asks;

    // orderbook.hpp
    std::set<int> m_active_asks;
    std::set<int> m_active_bids;
    
        // Кэш: order_id → (side, price_cents)
    std::unordered_map<uint64_t, std::pair<BookSide, int32_t>> m_order_metadata;

    // Пул ордеров
    OrderPool m_order_pool{1'000'000}; // на миллион ордеров
public:
    Orderbook(bool generate_dummies);

    void add_order(int qty, int32_t price, BookSide side);
    std::pair<int, double> handle_order(OrderType type, int order_quantity, Side side, int32_t price = 0);

    bool modify_order(uint64_t id, int new_qty);
    bool delete_order(uint64_t id);

    template <typename T>
    std::pair<int, double> fill_order(std::map<double, std::deque<std::unique_ptr<Order>>, T>& offers,
                                      const OrderType type, const Side side, int& order_quantity,
                                      int32_t price, int& units_transacted, double& total_value);

    int best_quote(BookSide side);

    const auto& get_bids() { return m_bids; }
    const auto& get_asks() { return m_asks; }

    template<typename T>
    void print_leg(std::map<double, std::deque<std::unique_ptr<Order>>, T>& orders, BookSide side);

    void print();
    void print_asks();
    void print_bids();
    // Правильно — без Orderbook::
    std::pair<int, double> fill_bids(int& order_quantity, int limit_price_cents, int& units_transacted, double& total_value);
    std::pair<int, double> fill_asks(int& order_quantity, int limit_price_cents, int& units_transacted, double& total_value);

    int m_best_bid = 0;
    int m_best_ask = MAX_PRICE_CENTS + 1;

};
