#include <iostream>
#include <cassert>
#include "../include/order.hpp"
#include "../include/helpers.hpp"
#include "../include/orderbook.hpp"

using namespace std;

// Function to test adding orders to the orderbook
void test_add_order() {
    Orderbook orderbook(false);

    // Add one bid order and one ask order
    orderbook.add_order(100, 10050, BookSide::bid);
    orderbook.add_order(200, 10100, BookSide::ask);

    // Use getter functions to access bids and asks
    const auto& bids = orderbook.get_bids();
    const auto& asks = orderbook.get_asks();

    // Check if the bid order was added correctly
    assert(bids.size() == 1);                   // Only one price level in bids
    assert(bids.at(10050).size() == 1);          // One order at price 10050
    assert(bids.at(10050)[0]->quantity == 100);  // Order quantity is 100
    assert(bids.at(10050)[0]->price_cents == 10050);   // Order price is 10050

    // Check if the ask order was added correctly
    assert(asks.size() == 1);                   // Only one price level in asks
    assert(asks.at(10100).size() == 1);          // One order at price 10100
    assert(asks.at(10100)[0]->quantity == 200);  // Order quantity is 200
    assert(asks.at(10100)[0]->price_cents == 10100);   // Order price is 10100

    cout << "test_add_order passed!" << endl;
}

// Function to test executing a market order
void test_execute_market_order() {
    Orderbook orderbook(false);

    // Add multiple bid orders
    orderbook.add_order(100, 10050, BookSide::bid);
    orderbook.add_order(150, 10050, BookSide::bid);
    // Add multiple ask orders (though market sell order works against bids)
    orderbook.add_order(200, 10100, BookSide::ask);
    orderbook.add_order(250, 10100, BookSide::ask);

    // Execute a market order to sell 200 units (should fill against bids)
    auto [units_transacted, total_value] = orderbook.handle_order(OrderType::market, 200, Side::sell, 0);

    const auto& bids = orderbook.get_bids();
    // Expect 200 units filled at 10050 price
    assert(units_transacted == 200);
    assert(total_value == 10050 * 200);

    // After filling, the bid orders at 10050 should be reduced:
    // Initially, there were two orders: one with 100 and one with 150 (total 250).
    // Filling 200 units should remove the first 100 completely and reduce the second from 150 to 50.
    assert(bids.at(10050).size() == 1);
    assert(bids.at(10050)[0]->quantity == 50);

    cout << "test_execute_market_order passed!" << endl;
}

// Function to test executing a limit order
void test_execute_limit_order() {
    Orderbook orderbook(false);

    // Add multiple bid and ask orders
    orderbook.add_order(100, 10050, BookSide::bid);
    orderbook.add_order(150, 10050, BookSide::bid);
    orderbook.add_order(200, 10100, BookSide::ask);
    orderbook.add_order(250, 10100, BookSide::ask);

    // Execute a limit order to buy 300 units at 10100
    auto [units_transacted, total_value] = orderbook.handle_order(OrderType::limit, 300, Side::buy, 10100);

    const auto& asks = orderbook.get_asks();
    // Expect 300 units filled at 10100 price level
    assert(units_transacted == 300);
    assert(total_value == 10100 * 300);

    // Initially there were two ask orders at 10100 (200 and 250 = 450).
    // Filling 300 should remove the 200-unit order entirely and reduce the 250-unit order to 150.
    assert(asks.at(10100).size() == 1);
    assert(asks.at(10100)[0]->quantity == 150);

    cout << "test_execute_limit_order passed!" << endl;
}

// Function to test the best bid and best ask prices
void test_best_quote() {
    Orderbook orderbook(false);

    orderbook.add_order(100, 10050, BookSide::bid);
    orderbook.add_order(200, 10100, BookSide::ask);

    double best_bid = orderbook.best_quote(BookSide::bid);
    double best_ask = orderbook.best_quote(BookSide::ask);

    assert(best_bid == 10050);
    assert(best_ask == 10100);

    cout << "test_best_quote passed!" << endl;
}

// Function to test a small market order against multiple ask orders
void test_small_market_order_best_ask() {
    Orderbook orderbook(false);

    // Add three ask orders at different prices
    orderbook.add_order(1000, 10100, BookSide::ask); // Best ask
    orderbook.add_order(1500, 10200, BookSide::ask);
    orderbook.add_order(2000, 10300, BookSide::ask);

    // Execute a market order to buy 100 units (should fill at best ask: 10100)
    auto [units_transacted, total_value] = orderbook.handle_order(OrderType::market, 100, Side::buy, 0);

    const auto& asks = orderbook.get_asks();
    assert(units_transacted == 100);
    assert(total_value == 10100 * 100);

    // The best ask at 10100 should be reduced from 1000 to 900
    assert(asks.at(10100)[0]->quantity == 900);
    // The orders at higher price levels should remain unchanged.
    assert(asks.at(10200)[0]->quantity == 1500);
    assert(asks.at(10300)[0]->quantity == 2000);

    cout << "test_small_market_order_best_ask passed!" << endl;
}

void test_modify_and_delete_order() {
    Orderbook orderbook(false);

    // Add an order. We'll capture its ID so we can modify/delete it.
    orderbook.add_order(100, 10050, BookSide::bid);

    // Retrieve the bids map and extract the first (and only) order at 10050
    const auto& bids = orderbook.get_bids();
    assert(!bids.empty());
    assert(bids.at(10050).size() == 1);

    // Capture the ID of this order
    uint64_t orderId = bids.at(10050)[0]->id;

    // ==========================
    // Time the modify_order call
    // ==========================
    uint64_t start_modify = unix_time();
    bool modified = orderbook.modify_order(orderId, 999);
    uint64_t end_modify = unix_time();

    // Confirm modify worked
    assert(modified && "modify_order should return true for a valid ID");
    assert(bids.at(10050)[0]->quantity == 999);

    // Print how long modify_order took
    cout << "modify_order took: " << (end_modify - start_modify) 
         << " ns" << endl;

    // ==========================
    // Time the delete_order call
    // ==========================
    uint64_t start_delete = unix_time();
    bool deleted = orderbook.delete_order(orderId);
    uint64_t end_delete = unix_time();

    // Confirm delete worked
    assert(deleted && "delete_order should return true for a valid ID");

    int price_cents = 10050;
    size_t idx = price_cents - MIN_PRICE_CENTS;
    // Verify that the order is gone
    if (idx < bids.size() && !bids[idx].empty()) {
        // Есть ордера на этой цене
        assert(bids.at(10050).empty());
    }


    // Print how long delete_order took
    cout << "delete_order took: " << (end_delete - start_delete)
         << " ns" << endl;

    cout << "test_modify_and_delete_order passed!" << endl;
}

// Main function to run all tests
int main() {
    test_add_order();
    test_execute_market_order();
    test_execute_limit_order();
    test_best_quote();
    test_small_market_order_best_ask();
    test_modify_and_delete_order();

    cout << "All tests passed!" << endl;
    return 0;
}
