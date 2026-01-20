    #include <iostream>
    #include <vector>
    #include <random>
    #include <cassert>
    #include <chrono>
    #include <algorithm>
    #include <fstream>

    // Include your existing headers
    #include "../include/helpers.hpp"
    #include "../include/enums.hpp"
    #include "../include/order.hpp"
    #include "../include/orderbook.hpp"

    using namespace std;

    int main() {
        // Create an empty orderbook (no dummy data)
        Orderbook orderbook(false);

        // Random engine setup
        std::mt19937 rng(std::random_device{}()); // Mersenne Twister
        std::uniform_int_distribution<int> qty_dist(100, 1000);
        std::uniform_int_distribution<int> side_dist(0, 1); // 0=buy, 1=sell

        // 1) Build 1000 price levels, each with 100 orders
        // Генерируем 10 000 УРОВНЕЙ на широком диапазоне
        const int NUM_LEVELS = 10000;
        const int MIN_TEST_PRICE = 5000;   // $50.00
        const int MAX_TEST_PRICE = 150000; // $1500.00

        std::uniform_int_distribution<int> price_dist(MIN_TEST_PRICE, MAX_TEST_PRICE);

        for (int i = 0; i < NUM_LEVELS; ++i) {
            int price = price_dist(rng); // случайная цена в диапазоне
            for (int j = 0; j < 10; ++j) { // по 10 ордеров на уровень
                int quantity = qty_dist(rng);
                BookSide side = (i % 2 == 0) ? BookSide::bid : BookSide::ask;
                orderbook.add_order(quantity, price, side);
            }
        }

        // Collect all IDs for modifies/deletes
        vector<uint64_t> all_ids;
        // Bids
        // Bids: идём от высоких цен к низким
        for (int price_cents = MAX_PRICE_CENTS; price_cents >= MIN_PRICE_CENTS; --price_cents) {
            size_t idx = price_cents - MIN_PRICE_CENTS;
            auto& dq = orderbook.get_bids()[idx];
            if (!dq.empty()) {
                for (auto& orderPtr : dq) {
                    all_ids.push_back(orderPtr->id);
                }
            }
        }

        // Asks: идём от низких цен к высоким
        for (int price_cents = MIN_PRICE_CENTS; price_cents <= MAX_PRICE_CENTS; ++price_cents) {
            size_t idx = price_cents - MIN_PRICE_CENTS;
            auto& dq = orderbook.get_asks()[idx];
            if (!dq.empty()) {
                for (auto& orderPtr : dq) {
                    all_ids.push_back(orderPtr->id);
                }
            }
        }

        // Shuffle them so they're not in strictly sorted or grouped order
        std::shuffle(all_ids.begin(), all_ids.end(), rng);
        cout << "Created " << all_ids.size() << " orders total." << endl;

        // ----------------------------------------------------------------------------------
        // Files to record times for distribution plotting
        // ----------------------------------------------------------------------------------
        ofstream marketTimesFile("market_times.txt");
        ofstream modifyTimesFile("modify_times.txt");
        ofstream deleteTimesFile("delete_times.txt");

        // ----------------------------------------------------------------------------------
        // 2) Random Market Orders
        // ----------------------------------------------------------------------------------
        const int NUM_MARKET_ORDERS = 5000;
        uint64_t total_market_ns = 0;

        std::uniform_int_distribution<int> market_qty_dist(100, 2000);
        for (int i = 0; i < NUM_MARKET_ORDERS; ++i) {
            // Random side (buy or sell)
            Side side = (side_dist(rng) == 0) ? Side::buy : Side::sell;
            int qty = market_qty_dist(rng);

            uint64_t start_t = unix_time();
            orderbook.handle_order(OrderType::market, qty, side);
            uint64_t end_t = unix_time();

            uint64_t duration = end_t - start_t;
            total_market_ns += duration;
            marketTimesFile << duration << "\n";
        }
        double avg_market_ns = static_cast<double>(total_market_ns) / NUM_MARKET_ORDERS;
        cout << "Average time for " << NUM_MARKET_ORDERS << " market orders: "
            << avg_market_ns << " ns\n";

        // ----------------------------------------------------------------------------------
        // 3) Random Modifies (biased towards the middle)
        // ----------------------------------------------------------------------------------
        const int NUM_MODIFIES = 500;
        uint64_t total_modify_ns = 0;

        // Normal distribution centered around the midpoint of all_ids
        double mean_index = all_ids.size() / 2.0;
        double stddev_index = all_ids.size() / 5.0; // stdev controls how 'wide' the distribution is
        std::normal_distribution<double> normal_dist_mod(mean_index, stddev_index);

        auto getMiddleBiasedID = [&](std::normal_distribution<double>& dist) {
            int idx = -1;
            // Keep drawing until we land inside [0, all_ids.size() - 1]
            do {
                double val = dist(rng);
                idx = static_cast<int>(std::round(val));
            } while (idx < 0 || idx >= static_cast<int>(all_ids.size()));
            return all_ids[idx];
        };

        for (int i = 0; i < NUM_MODIFIES; ++i) {
            // Random new quantity
            int new_qty = qty_dist(rng);

            // Biased pick: mostly near the middle, but sometimes edges
            uint64_t randomID = getMiddleBiasedID(normal_dist_mod);

            uint64_t start_t = unix_time();
            bool ok = orderbook.modify_order(randomID, new_qty);
            uint64_t end_t = unix_time();

            uint64_t duration = end_t - start_t;
            total_modify_ns += duration;
            modifyTimesFile << duration << "\n";
        }
        double avg_modify_ns = static_cast<double>(total_modify_ns) / NUM_MODIFIES;
        cout << "Average time for " << NUM_MODIFIES << " modifies: "
            << avg_modify_ns << " ns\n";

        // ----------------------------------------------------------------------------------
        // 4) Random Deletes (biased towards the middle)
        // ----------------------------------------------------------------------------------
        const int NUM_DELETES = 500;
        uint64_t total_delete_ns = 0;

        // We can reuse the same distribution. Or create a separate one if desired.
        std::normal_distribution<double> normal_dist_del(mean_index, stddev_index);

        for (int i = 0; i < NUM_DELETES; ++i) {
            // Pick random ID near the middle, occasionally hitting edges
            uint64_t randomID = getMiddleBiasedID(normal_dist_del);

            uint64_t start_t = unix_time();
            bool ok = orderbook.delete_order(randomID);
            uint64_t end_t = unix_time();

            uint64_t duration = end_t - start_t;
            total_delete_ns += duration;
            deleteTimesFile << duration << "\n";
        }
        double avg_delete_ns = static_cast<double>(total_delete_ns) / NUM_DELETES;
        cout << "Average time for " << NUM_DELETES << " deletes: "
            << avg_delete_ns << " ns\n";

        // Close output files for market, modify, and delete operations
        marketTimesFile.close();
        modifyTimesFile.close();
        deleteTimesFile.close();

        // ----------------------------------------------------------------------------------
        // 5) Random Limit Orders (near best bid/ask, but not improving them)
        // ----------------------------------------------------------------------------------
        const int NUM_LIMIT_ORDERS = 1000;
        ofstream limitTimesFile("limit_times.txt");
        uint64_t total_limit_ns = 0;

        // Normal distribution for price offset (adjust standard deviation as needed)
        std::normal_distribution<double> price_offset(0, 0.5); 

        for (int i = 0; i < NUM_LIMIT_ORDERS; ++i) {
            // Random side (buy or sell) for the limit order
            Side side = (side_dist(rng) == 0) ? Side::buy : Side::sell;
            int qty = market_qty_dist(rng);
            double limit_price = 0.0;

        int best_price_cents = -1;

        if (side == Side::buy) {
            best_price_cents = orderbook.best_quote(BookSide::bid);
            
            // Защита от отсутствия ордеров
            if (best_price_cents == -1) {
                limit_price = 50.00; // Фолбэк, если книги пустой
            } else {
                double best_bid = best_price_cents / 100.0;
                double offset = std::abs(price_offset(rng));
                limit_price = best_bid - offset;
            }
        } else { // Side::sell
            best_price_cents = orderbook.best_quote(BookSide::ask);
            
            // Защита от отсутствия ордеров
            if (best_price_cents == -1) {
                limit_price = 100.00; // Фолбэк
            } else {
                double best_ask = best_price_cents / 100.0;
                double offset = std::abs(price_offset(rng));
                limit_price = best_ask + offset;
            }
        }

        // Дополнительная защита на случай, если limit_price всё равно остался 0 или мусорным
        if (limit_price <= 0) {
            limit_price = 100.00; 
        }

            uint64_t start_t = unix_time();
            // Assumes handle_order can take a limit order with a specified price.
            orderbook.handle_order(OrderType::limit, qty, side, static_cast<int32_t>(limit_price * 100.0));
            uint64_t end_t = unix_time();

            uint64_t duration = end_t - start_t;
            total_limit_ns += duration;
            limitTimesFile << duration << "\n";
        }
        double avg_limit_ns = static_cast<double>(total_limit_ns) / NUM_LIMIT_ORDERS;
        cout << "Average time for " << NUM_LIMIT_ORDERS << " limit orders: "
            << avg_limit_ns << " ns\n";
        limitTimesFile.close();

        return 0;
    }
