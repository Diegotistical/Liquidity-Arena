/// @file matching_engine_test.cpp
/// @brief Unit tests for the MatchingEngine.

#include "matching_engine.hpp"
#include <gtest/gtest.h>
#include <vector>


using namespace arena;

class MatchingEngineTest : public ::testing::Test {
protected:
  OrderBook book;
  MatchingEngine engine{book};
  std::vector<FillMsg> fills;

  void SetUp() override {
    engine.set_fill_callback([this](const FillMsg &f) { fills.push_back(f); });
  }
};

TEST_F(MatchingEngineTest, LimitOrderRests) {
  // Non-crossing limit order should rest on the book.
  Order *o =
      engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(book.best_bid(), 10000);
  EXPECT_EQ(book.total_orders(), 1u);
  EXPECT_TRUE(fills.empty());
}

TEST_F(MatchingEngineTest, CrossingLimitFill) {
  // Place a resting ask.
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
  EXPECT_EQ(book.best_ask(), 10000);

  // Place a crossing bid at or above the ask.
  Order *o =
      engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);

  // Both should be fully filled.
  EXPECT_EQ(o, nullptr); // Fully filled, doesn't rest
  EXPECT_EQ(book.total_orders(), 0u);
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(fills[0].taker_id, 2u);
  EXPECT_EQ(fills[0].price, 10000);
  EXPECT_EQ(fills[0].quantity, 100);
}

TEST_F(MatchingEngineTest, PartialFill) {
  // Resting ask for 100.
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);

  // Crossing bid for 60 — partial fill.
  Order *o =
      engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 60, 0);
  EXPECT_EQ(o, nullptr); // Taker fully filled

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 60);

  // Maker should have 40 remaining.
  Order *maker = book.get_order(1);
  ASSERT_NE(maker, nullptr);
  EXPECT_EQ(maker->remaining(), 40);
  EXPECT_EQ(book.total_orders(), 1u);
}

TEST_F(MatchingEngineTest, TakerPartiallyFilled) {
  // Resting ask for 50.
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 50, 0);

  // Crossing bid for 100 — taker partially filled, remainder rests.
  Order *o =
      engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  ASSERT_NE(o, nullptr); // Taker has remaining quantity, rests

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 50);
  EXPECT_EQ(o->remaining(), 50);
  EXPECT_EQ(book.best_bid(), 10000);
}

TEST_F(MatchingEngineTest, PriceTimePriority) {
  // Place two asks at the same price. First one should be filled first (FIFO).
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100,
                           1); // Earlier
  engine.process_new_order(2, Side::ASK, OrderType::LIMIT, 10000, 100,
                           2); // Later

  // Crossing bid.
  engine.process_new_order(3, Side::BID, OrderType::LIMIT, 10000, 100, 3);

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].maker_id, 1u); // FIFO: order 1 should be filled first
}

TEST_F(MatchingEngineTest, PricePriorityBetterPriceFirst) {
  // Ask at 10000 and ask at 10100. Bid should fill at the better (lower) ask
  // first.
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10100, 100, 0);
  engine.process_new_order(2, Side::ASK, OrderType::LIMIT, 10000, 100, 0);

  engine.process_new_order(3, Side::BID, OrderType::LIMIT, 10100, 100, 0);

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].maker_id, 2u); // Better price (10000) filled first
  EXPECT_EQ(fills[0].price, 10000); // Filled at maker's price
}

TEST_F(MatchingEngineTest, MarketOrderFull) {
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);

  // Market buy — should fill immediately.
  Order *o =
      engine.process_new_order(2, Side::BID, OrderType::MARKET, 0, 100, 0);
  EXPECT_EQ(o, nullptr); // Market orders never rest

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 100);
}

TEST_F(MatchingEngineTest, MarketOrderPartialKilled) {
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 50, 0);

  // Market buy for 100, but only 50 available — fills 50, remainder killed.
  engine.process_new_order(2, Side::BID, OrderType::MARKET, 0, 100, 0);

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 50);
  EXPECT_EQ(book.total_orders(), 0u); // Market order doesn't rest
}

TEST_F(MatchingEngineTest, CancelOrder) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  EXPECT_TRUE(engine.process_cancel(1));
  EXPECT_EQ(book.total_orders(), 0u);
  EXPECT_EQ(book.best_bid(), INVALID_PRICE);

  // Cancel non-existent.
  EXPECT_FALSE(engine.process_cancel(999));
}

TEST_F(MatchingEngineTest, BookSnapshotAfterTrades) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 9900, 100, 0);
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 9800, 200, 0);
  engine.process_new_order(3, Side::ASK, OrderType::LIMIT, 10100, 150, 0);
  engine.process_new_order(4, Side::ASK, OrderType::LIMIT, 10200, 50, 0);

  BookUpdateMsg snap = engine.get_book_snapshot();
  EXPECT_EQ(snap.best_bid, 9900);
  EXPECT_EQ(snap.best_ask, 10100);
  EXPECT_EQ(snap.num_bid_levels, 2);
  EXPECT_EQ(snap.num_ask_levels, 2);
}

TEST_F(MatchingEngineTest, MultipleFillsWalkTheBook) {
  // Place asks at different price levels.
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 50, 0);
  engine.process_new_order(2, Side::ASK, OrderType::LIMIT, 10100, 50, 0);

  // Large crossing bid that walks both levels.
  engine.process_new_order(3, Side::BID, OrderType::LIMIT, 10100, 100, 0);

  // Should produce two fills.
  ASSERT_EQ(fills.size(), 2u);
  EXPECT_EQ(fills[0].price, 10000);
  EXPECT_EQ(fills[0].quantity, 50);
  EXPECT_EQ(fills[1].price, 10100);
  EXPECT_EQ(fills[1].quantity, 50);
}

TEST_F(MatchingEngineTest, Statistics) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
  engine.process_cancel(999); // No-op cancel

  EXPECT_EQ(engine.total_orders(), 2u);
  EXPECT_EQ(engine.total_fills(), 1u);
  EXPECT_EQ(engine.total_cancels(), 0u); // Failed cancel doesn't count
}
