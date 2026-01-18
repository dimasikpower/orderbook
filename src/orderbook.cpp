/**
 * @file order.cpp
 * @brief This file contains the implementation of the Orderbook class.
 */

#include <iostream>
#include <chrono>
#include <stdlib.h>
#include <map>
#include <thread>
#include <iomanip>
#include <memory>
#include <deque>

#include "../include/order.hpp"
#include "../include/orderbook.hpp"

using namespace std;

void Orderbook::add_order(int qty, int32_t price_cents, BookSide side) {
    // Проверка границ
    if (price_cents < MIN_PRICE_CENTS || price_cents > MAX_PRICE_CENTS) {
        return; // или бросить исключение
    }

    size_t idx = price_cents - MIN_PRICE_CENTS;

    // Создаём ордер
    auto order = std::make_unique<Order>(qty, price_cents, side);
    uint64_t order_id = order->id;

    // Добавляем в соответствующую книгу
    if (side == BookSide::bid) {
        m_bids[idx].push_back(std::move(order));
        m_active_bids.insert(price_cents); // добавляем в активные уровни
        if (price_cents > m_best_bid) {
            m_best_bid = price_cents;
        }
    } else { // BookSide::ask
        m_asks[idx].push_back(std::move(order));
        m_active_asks.insert(price_cents);
        if (price_cents < m_best_ask) {
            m_best_ask = price_cents;
        }
    }

    // Кэшируем ID для modify/delete
    m_order_metadata[order_id] = std::make_pair(side, price_cents);
}


Orderbook::Orderbook(bool generate_dummies)
    : m_bids(MAX_PRICE_CENTS + 1), m_asks(MAX_PRICE_CENTS + 1)
{
    srand(12);

    if (generate_dummies) {
        // Dummy bids: $90.00 – $100.00 → 9000–10000 центов
        for (int i = 0; i < 3; i++) {
            int random_price_cents = 9000 + (rand() % 1001);
            int random_qty = rand() % 100 + 1;
            int random_qty2 = rand() % 100 + 1;
            
            add_order(random_qty, random_price_cents, BookSide::bid);
            this_thread::sleep_for(chrono::milliseconds(1));
            add_order(random_qty2, random_price_cents, BookSide::bid);
        }

        // Dummy asks: $100.00 – $110.00 → 10000–11000 центов
        for (int i = 0; i < 3; i++) {
            int random_price_cents = 10000 + (rand() % 1001);
            int random_qty = rand() % 100 + 1;
            int random_qty2 = rand() % 100 + 1;
            
            add_order(random_qty, random_price_cents, BookSide::ask);
            this_thread::sleep_for(chrono::milliseconds(1));
            add_order(random_qty2, random_price_cents, BookSide::ask);
        }
    }
}

// Template function to fill orders from the offers (deque) at each price level
template <typename T>
std::pair<int, double> Orderbook::fill_order(map<double, deque<unique_ptr<Order>>, T>& offers, 
                                               const OrderType type, const Side side, int& order_quantity,
                                               const int32_t price, int& units_transacted, double& total_value) {
    // Iterate over the price levels (best prices first)
    auto rit = offers.begin();
    while(rit != offers.end()) {
        const double price_level = rit->first;
        auto& orders = rit->second;

        // For a limit order, ensure the price level is acceptable
        // market order always acceptable price
        bool can_transact = true;
        if (type == OrderType::limit) {
            if (side == Side::buy && price_level > price) {
                can_transact = false;
            } else if (side == Side::sell && price_level < price) {
                can_transact = false;
            }
        }

        if (can_transact) {
            // Process orders at this price level while there are orders and the incoming order is not fully filled
            while (!orders.empty() && order_quantity > 0) {
                auto& current_order = orders.front();
                const u_int64_t order_id = current_order->id;
                int current_qty = current_order->quantity;
                double current_price = current_order->price;

                if (current_qty > order_quantity) { // Partial fill
                    units_transacted += order_quantity;
                    total_value += order_quantity * current_price;
                    current_order->quantity = current_qty - order_quantity;
                    order_quantity = 0;
                    break; // Incoming order fully filled
                } else { // Full fill
                    units_transacted += current_qty;
                    total_value += current_qty * current_price;
                    order_quantity -= current_qty;
                    orders.pop_front();
                    // clean cache
                    m_order_metadata.erase(order_id);
                }
            }
            
            // remove map entry if we wiped all the orders 
            if (orders.empty()){
                rit = offers.erase(rit);
            }else{
                if (order_quantity > 0) ++rit;
                else break;
            }
        }else{
            // Prices will only get worse, break
            break;
        }
    }
    
    return std::make_pair(units_transacted, total_value);
}

// Handles market and limit orders, returning the total units transacted and total value
std::pair<int, double> Orderbook::handle_order(OrderType type, int order_quantity, Side side, int32_t price) {
    int units_transacted = 0;
    double total_value = 0;

    if (type == OrderType::market) {
        if (side == Side::sell) {
            // return fill_order(m_bids, OrderType::market, Side::sell, order_quantity, price, units_transacted, total_value);
            return fill_bids(order_quantity, -1, units_transacted, total_value);
        } else if (side == Side::buy) {
            return fill_asks(order_quantity, -1, units_transacted, total_value);
            // return fill_order(m_asks, OrderType::market, Side::buy, order_quantity, price, units_transacted, total_value);
        }
    } else if (type == OrderType::limit) {
        if (side == Side::buy) {
            int best_ask = m_active_asks.empty() ? -1 : m_best_ask;
            if (best_ask != -1 && best_ask <= price) {
                auto fill = fill_asks(order_quantity, price, units_transacted, total_value);
                if (order_quantity > 0)
                    add_order(order_quantity, price, BookSide::bid);
                return fill;
            } else {
                add_order(order_quantity, price, BookSide::bid);
                return {units_transacted, total_value};
            }
        } else { // Side::sell
            int best_bid = m_active_bids.empty() ? -1 : m_best_bid;
            if (best_bid != -1 && best_bid >= price) {
                auto fill = fill_bids(order_quantity, price, units_transacted, total_value);
                if (order_quantity > 0)
                    add_order(order_quantity, price, BookSide::ask);
                return fill;
            } else {
                add_order(order_quantity, price, BookSide::ask);
                return {units_transacted, total_value};
            }
        }
    } else {
        throw std::runtime_error("Invalid order type encountered");
    }
    return std::make_pair(units_transacted, total_value);
}

// Returns the best quote (price) for the given book side
// double Orderbook::best_quote(BookSide side) {
//     if (side == BookSide::bid) {
//         return m_bids.begin()->first;
//     } else if (side == BookSide::ask) {
//         return m_asks.begin()->first;
//     } else {
//         return 0.0;
//     }
// }

int Orderbook::best_quote(BookSide side) {
    if (side == BookSide::bid) {
        for (int p = MAX_PRICE_CENTS; p >= MIN_PRICE_CENTS; --p) {
            if (!m_bids[p - MIN_PRICE_CENTS].empty()) {
                return p;
            }
        }
    } else if (side == BookSide::ask) {
        for (int p = MIN_PRICE_CENTS; p <= MAX_PRICE_CENTS; ++p) {
            if (!m_asks[p - MIN_PRICE_CENTS].empty()) {
                return p;
            }
        }
    }
    return -1; // нет ордеров
}

// Search through whole book and modify the target order
bool Orderbook::modify_order(uint64_t id, int new_qty) {
    if (m_order_metadata.find(id) == m_order_metadata.end()) {
        return false;
    }

    auto [side, price_cents] = m_order_metadata[id];
    size_t idx = price_cents - MIN_PRICE_CENTS;

    auto modify_in_vector = [&](auto& order_levels) -> bool {
        if (idx >= order_levels.size()) return false;
        for (auto& order : order_levels[idx]) {
            if (order->id == id) {
                order->quantity = new_qty;
                return true;
            }
        }
        return false;
    };

    if (side == BookSide::ask) {
        return modify_in_vector(m_asks);
    } else if (side == BookSide::bid) {
        return modify_in_vector(m_bids);
    }
    return false;
}

// Sweep through the book 
bool Orderbook::delete_order(uint64_t id) {
    // Убедись, что такой ордер существует
    if (m_order_metadata.find(id) == m_order_metadata.end()) {
        return false;
    }

    auto [side, price_cents] = m_order_metadata[id];
    m_order_metadata.erase(id); // удаляем из кэша

    size_t idx = price_cents - MIN_PRICE_CENTS;

    auto remove_from_vector = [&](std::vector<std::deque<std::unique_ptr<Order>>>& order_levels) -> bool {
        if (idx >= order_levels.size()) return false;

        auto& orders = order_levels[idx];
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if ((*it)->id == id) {
                orders.erase(it);
                // НЕТ orders_map.erase(price) — вектору это не нужно!
                return true;
            }
        }
        return false;
    };

    if (side == BookSide::bid) {
        return remove_from_vector(m_bids);
    } else if (side == BookSide::ask) {
        return remove_from_vector(m_asks);
    }
    return false;
}

// Template function to print a leg (bid or ask) of the order book.
template<typename T>
void Orderbook::print_leg(map<double, deque<unique_ptr<Order>>, T>& hashmap, BookSide side) {
    if (side == BookSide::ask) {
        for (auto it = hashmap.rbegin(); it != hashmap.rend(); ++it) { // iterate over price levels
            int size_sum = 0;
            for (auto& order : it->second) {
                size_sum += order->quantity;
            }
            string color = "31"; // red for asks
            cout << "\t\033[1;" << color << "m" << "$" << setw(6) << fixed << setprecision(2)
                 << it->first << setw(5) << size_sum << "\033[0m ";
            for (int i = 0; i < size_sum / 10; i++) {
                cout << "█";
            }
            cout << "\n";
        }
    } else if (side == BookSide::bid) {
        for (auto it = hashmap.begin(); it != hashmap.end(); ++it) {
            int size_sum = 0;
            for (auto& order : it->second) {
                size_sum += order->quantity;
            }
            string color = "32"; // green for bids
            cout << "\t\033[1;" << color << "m" << "$" << setw(6) << fixed << setprecision(2)
                 << it->first << setw(5) << size_sum << "\033[0m ";
            for (int i = 0; i < size_sum / 10; i++) {
                cout << "█";
            }
            cout << "\n";
        }
    }
}

// void Orderbook::print() {
//     cout << "========== Orderbook =========" << "\n";
//     print_leg(m_asks, BookSide::ask);

//     // Print bid-ask spread (in basis points)
//     double best_ask = best_quote(BookSide::ask);
//     double best_bid = best_quote(BookSide::bid);
//     cout << "\n\033[1;33m" << "======  " << 10000 * (best_ask - best_bid) / best_bid << "bps  ======\033[0m\n\n";

//     print_leg(m_bids, BookSide::bid);
//     cout << "==============================\n\n\n";
// }

// Для покупок (bids) — идём от высоких цен к низким
std::pair<int, double> Orderbook::fill_bids(int& order_quantity, int limit_price, int& units_transacted, double& total_value) {
    // Если нет активных bid-уровней — выходим
    if (m_active_bids.empty()) {
        return {units_transacted, total_value};
    }

    // Идём от самой высокой цены к самой низкой
    for (auto it = m_active_bids.rbegin(); it != m_active_bids.rend(); ++it) {
        int price_cents = *it;

        // Для лимитного sell: если цена ниже лимита — выходим
        if (limit_price > 0 && price_cents < limit_price) {
            break;
        }

        size_t idx = price_cents - MIN_PRICE_CENTS;
        auto& orders = m_bids[idx];

        while (!orders.empty() && order_quantity > 0) {
            auto& current_order = orders.front();
            int available_qty = current_order->quantity;

            if (available_qty > order_quantity) {
                // Частичное исполнение
                units_transacted += order_quantity;
                total_value += (order_quantity * price_cents) / 100.0;
                current_order->quantity -= order_quantity;
                order_quantity = 0;
                return {units_transacted, total_value};
            } else {
                // Полное исполнение
                units_transacted += available_qty;
                total_value += (available_qty * price_cents) / 100.0;
                order_quantity -= available_qty;
                orders.pop_front();
                m_order_metadata.erase(current_order->id);

                // Если уровень стал пустым — удаляем из активных
                if (orders.empty()) {
                    m_active_bids.erase(price_cents);
                    // Обновляем лучший bid
                    if (price_cents == m_best_bid) {
                        if (m_active_bids.empty()) {
                            m_best_bid = 0;
                        } else {
                            m_best_bid = *m_active_bids.rbegin(); // самый высокий оставшийся
                        }
                    }
                }
            }
        }

        if (order_quantity == 0) break;
    }

    return {units_transacted, total_value};
}



std::pair<int, double> Orderbook::fill_asks(int& order_quantity, int limit_price, int& units_transacted, double& total_value) {
    if (m_active_asks.empty()) {
        return {units_transacted, total_value};
    }

    for (int price_cents : m_active_asks) {
        // Если лимит задан и цена слишком высока — выходим
        if (limit_price > 0 && price_cents > limit_price) {
            break;
        }

        size_t idx = price_cents - MIN_PRICE_CENTS;
        auto& orders = m_asks[idx];

        while (!orders.empty() && order_quantity > 0) {
            auto& current_order = orders.front();
            int available_qty = current_order->quantity;

            if (available_qty > order_quantity) {
                // Частичное исполнение
                units_transacted += order_quantity;
                total_value += (order_quantity * price_cents) / 100.0;
                current_order->quantity -= order_quantity;
                order_quantity = 0;
                return {units_transacted, total_value};
            } else {
                // Полное исполнение
                units_transacted += available_qty;
                total_value += (available_qty * price_cents) / 100.0;
                order_quantity -= available_qty;
                orders.pop_front();
                m_order_metadata.erase(current_order->id);

                // Если уровень опустел — удаляем из активных
                if (orders.empty()) {
                    m_active_asks.erase(price_cents);
                    if (price_cents == m_best_ask) {
                        m_best_ask = m_active_asks.empty() ? (MAX_PRICE_CENTS + 1) : *m_active_asks.begin();
                    }
                }
            }
        }

        if (order_quantity == 0) break;
    }

    return {units_transacted, total_value};
}

void Orderbook::print_bids() {
    for (int price_cents = MIN_PRICE_CENTS; price_cents <= MAX_PRICE_CENTS; ++price_cents) {
        auto& orders = m_bids[price_cents - MIN_PRICE_CENTS];
        if (orders.empty()) continue;

        int size_sum = 0;
        for (auto& order : orders) {
            size_sum += order->quantity;
        }

        double price = price_cents / 100.0;
        cout << "\t\033[1;32m$" << setw(6) << fixed << setprecision(2)
             << price << setw(5) << size_sum << "\033[0m ";
        for (int i = 0; i < size_sum / 10; i++) cout << "█";
        cout << "\n";
    }
}

void Orderbook::print_asks() {
    for (int price_cents = MAX_PRICE_CENTS; price_cents >= MIN_PRICE_CENTS; --price_cents) {
        auto& orders = m_asks[price_cents - MIN_PRICE_CENTS];
        if (orders.empty()) continue;

        int size_sum = 0;
        for (auto& order : orders) {
            size_sum += order->quantity;
        }

        double price = price_cents / 100.0;
        cout << "\t\033[1;31m$" << setw(6) << fixed << setprecision(2)
             << price << setw(5) << size_sum << "\033[0m ";
        for (int i = 0; i < size_sum / 10; i++) cout << "█";
        cout << "\n";
    }
}

void Orderbook::print() {
    cout << "========== Orderbook =========" << "\n";
    print_asks();

    int best_ask_cents = best_quote(BookSide::ask);
    int best_bid_cents = best_quote(BookSide::bid);

    double best_ask = (best_ask_cents == -1) ? 0.0 : best_ask_cents / 100.0;
    double best_bid = (best_bid_cents == -1) ? 0.0 : best_bid_cents / 100.0;
    cout << "\n\033[1;33m======  " << 10000 * (best_ask - best_bid) / best_bid << "bps  ======\033[0m\n\n";

    print_bids();
    cout << "==============================\n\n\n";
}