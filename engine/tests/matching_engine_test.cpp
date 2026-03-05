/// @file matching_engine_test.cpp
/// @brief Unit tests for the MatchingEngine.
///
/// Covers: limit orders, market orders, partial fills, price-time priority,
/// order modification, iceberg orders, maker/taker fees, and statistics.

#include "matching_engine.hpp"

#include <vector>

#include <gtest/gtest.h>

using namespace arena;

class MatchingEngineTest : public ::testing::Test {
protected:
  OrderBook book;
  MatchingEngine engine{book};
  std::vector<FillMsg> fills;

  void SetUp() override {
    engine.set_fill_callback([this](const FillMsg& f) { fills.push_back(f); });
  }
};

// ═══════════════════════════════════════════════════════════════════════
// Basic Limit Order Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, LimitOrderRests) {
  // Non-crossing limit order should rest on the book.
  Order *o = engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
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
  Order *o = engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);

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
  Order *o = engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 60, 0);
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
  Order *o = engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  ASSERT_NE(o, nullptr); // Taker has remaining quantity, rests

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 50);
  EXPECT_EQ(o->remaining(), 50);
  EXPECT_EQ(book.best_bid(), 10000);
}

// ═══════════════════════════════════════════════════════════════════════
// Price-Time Priority Tests
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// Market Order Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, MarketOrderFull) {
  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);

  // Market buy — should fill immediately.
  Order *o = engine.process_new_order(2, Side::BID, OrderType::MARKET, 0, 100, 0);
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

// ═══════════════════════════════════════════════════════════════════════
// Cancel Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, CancelOrder) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  EXPECT_TRUE(engine.process_cancel(1));
  EXPECT_EQ(book.total_orders(), 0u);
  EXPECT_EQ(book.best_bid(), INVALID_PRICE);

  // Cancel non-existent.
  EXPECT_FALSE(engine.process_cancel(999));
}

// ═══════════════════════════════════════════════════════════════════════
// Order Modify Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, ModifyQuantityDown) {
  // Place order, then reduce quantity (should keep queue position).
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  EXPECT_TRUE(engine.process_modify(1, 10000, 50));

  Order *o = book.get_order(1);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->remaining(), 50);
  EXPECT_EQ(o->price, 10000); // Price unchanged
}

TEST_F(MatchingEngineTest, ModifyPriceChangeLosesQueuePosition) {
  // Place two orders at same price.
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);

  // Modify order 1's price — it should lose queue position.
  EXPECT_TRUE(engine.process_modify(1, 10100, 100));

  Order *o = book.get_order(1);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->price, 10100);
  EXPECT_EQ(book.best_bid(), 10100); // Now best bid
}

TEST_F(MatchingEngineTest, ModifyQuantityUpRejected) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  // Quantity increase should be rejected.
  EXPECT_FALSE(engine.process_modify(1, 10000, 200));
}

TEST_F(MatchingEngineTest, ModifyNonExistent) {
  EXPECT_FALSE(engine.process_modify(999, 10000, 100));
}

// ═══════════════════════════════════════════════════════════════════════
// Iceberg Order Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, IcebergOrderRests) {
  // Place an iceberg ask: 500 total, 100 display.
  Order *o = engine.process_new_order(1, Side::ASK, OrderType::ICEBERG, 10000, 500, 0, 100);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->type, OrderType::ICEBERG);
  EXPECT_EQ(o->display_qty, 100);
  EXPECT_EQ(o->hidden_qty, 400);
  EXPECT_EQ(book.best_ask(), 10000);
}

TEST_F(MatchingEngineTest, IcebergPartialFillDisplay) {
  // Place iceberg ask: 500 total, 100 display.
  engine.process_new_order(1, Side::ASK, OrderType::ICEBERG, 10000, 500, 0, 100);

  // Buy 50 — should fill against display portion.
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 50, 0);

  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 50);
}

TEST_F(MatchingEngineTest, IcebergInvalidParams) {
  // Iceberg with display_qty = 0 should be rejected.
  Order *o = engine.process_new_order(1, Side::ASK, OrderType::ICEBERG, 10000, 500, 0, 0);
  EXPECT_EQ(o, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════
// Fee Model Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, FeesAppliedOnFill) {
  // Set fees: 2 bps maker rebate, 3 bps taker fee.
  FeeSchedule fees;
  fees.maker_rebate_bps = 2.0;
  fees.taker_fee_bps = 3.0;
  engine.set_fee_schedule(fees);

  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);

  ASSERT_EQ(fills.size(), 1u);
  // maker_fee should be negative (rebate earned).
  EXPECT_LT(fills[0].maker_fee, 0.0);
  // taker_fee should be positive (fee paid).
  EXPECT_GT(fills[0].taker_fee, 0.0);
}

TEST_F(MatchingEngineTest, FeeScheduleAccumulates) {
  FeeSchedule fees;
  fees.maker_rebate_bps = 2.0;
  fees.taker_fee_bps = 3.0;
  engine.set_fee_schedule(fees);

  engine.process_new_order(1, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 100, 0);

  EXPECT_GT(engine.total_maker_rebates(), 0.0);
  EXPECT_GT(engine.total_taker_fees(), 0.0);
}

// ═══════════════════════════════════════════════════════════════════════
// Queue Position Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, QueueVolumeAhead) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::BID, OrderType::LIMIT, 10000, 200, 0);
  engine.process_new_order(3, Side::BID, OrderType::LIMIT, 10000, 50, 0);

  PriceLevel *level = book.get_level(Side::BID, 10000);
  ASSERT_NE(level, nullptr);

  // Order 1 is at front — 0 ahead.
  Order *o1 = book.get_order(1);
  EXPECT_EQ(level->queue_volume_ahead(o1), 0);

  // Order 2 has 100 ahead (order 1).
  Order *o2 = book.get_order(2);
  EXPECT_EQ(level->queue_volume_ahead(o2), 100);

  // Order 3 has 300 ahead (100 + 200).
  Order *o3 = book.get_order(3);
  EXPECT_EQ(level->queue_volume_ahead(o3), 300);
}

// ═══════════════════════════════════════════════════════════════════════
// Book Snapshot Tests
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// Statistics Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, Statistics) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  engine.process_new_order(2, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
  engine.process_cancel(999); // No-op cancel

  EXPECT_EQ(engine.total_orders(), 2u);
  EXPECT_EQ(engine.total_fills(), 1u);
  EXPECT_EQ(engine.total_cancels(), 0u); // Failed cancel doesn't count
}

TEST_F(MatchingEngineTest, ModifyStatistics) {
  engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 100, 0);
  engine.process_modify(1, 10000, 50);
  engine.process_modify(999, 10000, 50); // Non-existent — doesn't count

  EXPECT_EQ(engine.total_modifies(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════
// Input Validation Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(MatchingEngineTest, RejectZeroQuantity) {
  Order *o = engine.process_new_order(1, Side::BID, OrderType::LIMIT, 10000, 0, 0);
  EXPECT_EQ(o, nullptr);
}

TEST_F(MatchingEngineTest, RejectNegativePrice) {
  Order *o = engine.process_new_order(1, Side::BID, OrderType::LIMIT, -100, 100, 0);
  EXPECT_EQ(o, nullptr);
}
