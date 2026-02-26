/// @file order_book_test.cpp
/// @brief Unit tests for OrderBook.

#include "order_book.hpp"
#include <gtest/gtest.h>


using namespace arena;

TEST(OrderBookTest, EmptyBook) {
  OrderBook book;
  EXPECT_EQ(book.best_bid(), INVALID_PRICE);
  EXPECT_EQ(book.best_ask(), INVALID_PRICE);
  EXPECT_EQ(book.total_orders(), 0u);
  EXPECT_EQ(book.best_bid_order(), nullptr);
  EXPECT_EQ(book.best_ask_order(), nullptr);
}

TEST(OrderBookTest, AddBidOrder) {
  OrderBook book;
  Order *o = book.add_order(1, Side::BID, 10000, 100, 0);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(book.best_bid(), 10000);
  EXPECT_EQ(book.best_ask(), INVALID_PRICE);
  EXPECT_EQ(book.total_orders(), 1u);
}

TEST(OrderBookTest, AddAskOrder) {
  OrderBook book;
  Order *o = book.add_order(1, Side::ASK, 10100, 50, 0);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(book.best_ask(), 10100);
  EXPECT_EQ(book.best_bid(), INVALID_PRICE);
}

TEST(OrderBookTest, BestBidTracking) {
  OrderBook book;
  book.add_order(1, Side::BID, 9900, 100, 0);
  book.add_order(2, Side::BID, 10000, 100, 0);
  book.add_order(3, Side::BID, 9800, 100, 0);

  // Best bid should be the highest.
  EXPECT_EQ(book.best_bid(), 10000);
}

TEST(OrderBookTest, BestAskTracking) {
  OrderBook book;
  book.add_order(1, Side::ASK, 10200, 100, 0);
  book.add_order(2, Side::ASK, 10100, 100, 0);
  book.add_order(3, Side::ASK, 10300, 100, 0);

  // Best ask should be the lowest.
  EXPECT_EQ(book.best_ask(), 10100);
}

TEST(OrderBookTest, CancelOrder) {
  OrderBook book;
  book.add_order(1, Side::BID, 10000, 100, 0);
  book.add_order(2, Side::BID, 9900, 100, 0);

  EXPECT_TRUE(book.cancel_order(1));
  EXPECT_EQ(book.best_bid(), 9900);
  EXPECT_EQ(book.total_orders(), 1u);

  // Cancel non-existent order.
  EXPECT_FALSE(book.cancel_order(999));
}

TEST(OrderBookTest, CancelLastOrderAtBestPrice) {
  OrderBook book;
  book.add_order(1, Side::BID, 10000, 100, 0);

  book.cancel_order(1);
  EXPECT_EQ(book.best_bid(), INVALID_PRICE);
  EXPECT_EQ(book.total_orders(), 0u);
}

TEST(OrderBookTest, GetDepthBids) {
  OrderBook book;
  book.add_order(1, Side::BID, 10000, 100, 0);
  book.add_order(2, Side::BID, 9900, 200, 0);
  book.add_order(3, Side::BID, 9800, 150, 0);
  book.add_order(4, Side::BID, 10000, 50, 0); // Same level as order 1

  auto depth = book.get_depth(Side::BID, 3);
  ASSERT_EQ(depth.size(), 3u);

  // Should be sorted descending.
  EXPECT_EQ(depth[0].price, 10000);
  EXPECT_EQ(depth[0].quantity, 150); // 100 + 50
  EXPECT_EQ(depth[1].price, 9900);
  EXPECT_EQ(depth[1].quantity, 200);
  EXPECT_EQ(depth[2].price, 9800);
  EXPECT_EQ(depth[2].quantity, 150);
}

TEST(OrderBookTest, GetDepthAsks) {
  OrderBook book;
  book.add_order(1, Side::ASK, 10100, 100, 0);
  book.add_order(2, Side::ASK, 10200, 200, 0);

  auto depth = book.get_depth(Side::ASK, 5);
  ASSERT_EQ(depth.size(), 2u);

  // Should be sorted ascending.
  EXPECT_EQ(depth[0].price, 10100);
  EXPECT_EQ(depth[1].price, 10200);
}

TEST(OrderBookTest, GetOrderById) {
  OrderBook book;
  book.add_order(42, Side::BID, 10000, 100, 0);

  Order *found = book.get_order(42);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->id, 42u);
  EXPECT_EQ(found->price, 10000);

  EXPECT_EQ(book.get_order(999), nullptr);
}

TEST(OrderBookTest, MultipleOrdersSameLevel) {
  OrderBook book;
  book.add_order(1, Side::ASK, 10100, 100, 0);
  book.add_order(2, Side::ASK, 10100, 200, 0);

  EXPECT_EQ(book.ask_levels(), 1u);
  EXPECT_EQ(book.total_orders(), 2u);

  auto depth = book.get_depth(Side::ASK, 1);
  EXPECT_EQ(depth[0].quantity, 300); // Aggregated
}
