#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include "../include/orderbook.hpp"

// Точное измерение через rdtsc (если x86)
#ifdef __x86_64__
#include <x86intrin.h>
uint64_t now() { return __rdtsc(); }
double cycles_to_ns(uint64_t cycles) {
    // Предположим, CPU = 3 GHz → 1 такт = 0.333 нс
    return cycles / 3.0;
}
#else
auto now() { return std::chrono::high_resolution_clock::now(); }
#endif

int main() {
    const int NUM_ORDERS = 10000;
    const int MAX_PRICE = 10000; // $100.00

    // --- Этап 1: заполнение книги ---
    Orderbook book(false);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> qty_dist(10, 1000);
    std::uniform_int_distribution<> price_dist(9000, 11000); // $90–$110

    auto start = now();
    for (int i = 0; i < NUM_ORDERS / 2; ++i) {
        book.add_order(qty_dist(gen), price_dist(gen), BookSide::bid);
        book.add_order(qty_dist(gen), price_dist(gen), BookSide::ask);
    }
    auto fill_time = now() - start;

    // --- Этап 2: исполнение market-ордеров ---
    std::uniform_int_distribution<> market_qty(100, 5000);
    start = now();
    for (int i = 0; i < 1000; ++i) {
        book.handle_order(OrderType::market, market_qty(gen), Side::buy);
        book.handle_order(OrderType::market, market_qty(gen), Side::sell);
    }
    auto exec_time = now() - start;

    // --- Вывод результатов ---
#ifdef __x86_64__
    std::cout << "Fill time: " << cycles_to_ns(fill_time) / 1000.0 << " mcs\n";
    std::cout << "Exec time per order: " << cycles_to_ns(exec_time) / 2000.0 << " ns\n";
#else
    auto to_micros = [](auto dur) {
        return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
    };
    std::cout << "Fill time: " << to_micros(fill_time) << " mcs\n";
    std::cout << "Exec time per order: " << to_micros(exec_time) * 1000 / 2000 << " ns\n";
#endif

    // Пример: как получить лучшую цену
    int best_bid = book.best_quote(BookSide::bid);
    int best_ask = book.best_quote(BookSide::ask);
    std::cout << "Best bid: $" << (best_bid > 0 ? best_bid / 100.0 : 0.0)
              << ", Best ask: $" << (best_ask > 0 ? best_ask / 100.0 : 0.0) << "\n";

    return 0;
}