/// @file price_level_test.cpp
/// @brief Unit tests for PriceLevel (intrusive doubly-linked list).

#include "price_level.hpp"
#include <gtest/gtest.h>


using namespace arena;

class PriceLevelTest : public ::testing::Test {
protected:
  // Pre-allocate orders on the stack for testing (no pool needed here).
  Order orders_[10];

  void SetUp() override {
    for (int i = 0; i < 10; ++i) {
      orders_[i].reset();
      orders_[i].id = static_cast<OrderId>(i + 1);
      orders_[i].quantity = 100;
      orders_[i].price = 10000;
    }
  }
};

TEST_F(PriceLevelTest, EmptyLevel) {
  PriceLevel level;
  EXPECT_TRUE(level.empty());
  EXPECT_EQ(level.order_count(), 0u);
  EXPECT_EQ(level.total_quantity(), 0);
  EXPECT_EQ(level.front(), nullptr);
}

TEST_F(PriceLevelTest, PushBackAndFront) {
  PriceLevel level;
  level.push_back(&orders_[0]);

  EXPECT_FALSE(level.empty());
  EXPECT_EQ(level.order_count(), 1u);
  EXPECT_EQ(level.total_quantity(), 100);
  EXPECT_EQ(level.front(), &orders_[0]);
}

TEST_F(PriceLevelTest, FIFOOrder) {
  PriceLevel level;
  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);
  level.push_back(&orders_[2]);

  // Front should be the first order added (FIFO).
  EXPECT_EQ(level.front()->id, 1u);
  EXPECT_EQ(level.order_count(), 3u);
  EXPECT_EQ(level.total_quantity(), 300);
}

TEST_F(PriceLevelTest, PopFront) {
  PriceLevel level;
  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);

  Order *popped = level.pop_front();
  EXPECT_EQ(popped->id, 1u);
  EXPECT_EQ(level.front()->id, 2u);
  EXPECT_EQ(level.order_count(), 1u);
  EXPECT_EQ(level.total_quantity(), 100);

  Order *popped2 = level.pop_front();
  EXPECT_EQ(popped2->id, 2u);
  EXPECT_TRUE(level.empty());
  EXPECT_EQ(level.pop_front(), nullptr);
}

TEST_F(PriceLevelTest, RemoveMiddle) {
  PriceLevel level;
  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);
  level.push_back(&orders_[2]);

  // Remove the middle order.
  level.remove(&orders_[1]);
  EXPECT_EQ(level.order_count(), 2u);
  EXPECT_EQ(level.total_quantity(), 200);
  EXPECT_EQ(level.front()->id, 1u);

  // Verify linked list integrity: order[0].next should now be order[2].
  EXPECT_EQ(orders_[0].next, &orders_[2]);
  EXPECT_EQ(orders_[2].prev, &orders_[0]);
}

TEST_F(PriceLevelTest, RemoveHead) {
  PriceLevel level;
  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);

  level.remove(&orders_[0]);
  EXPECT_EQ(level.front()->id, 2u);
  EXPECT_EQ(level.order_count(), 1u);
}

TEST_F(PriceLevelTest, RemoveTail) {
  PriceLevel level;
  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);

  level.remove(&orders_[1]);
  EXPECT_EQ(level.front()->id, 1u);
  EXPECT_EQ(level.order_count(), 1u);

  // Pop the remaining order to verify tail was updated correctly.
  Order *last = level.pop_front();
  EXPECT_EQ(last->id, 1u);
  EXPECT_TRUE(level.empty());
}

TEST_F(PriceLevelTest, ReduceQuantity) {
  PriceLevel level;
  orders_[0].quantity = 200;
  level.push_back(&orders_[0]);
  EXPECT_EQ(level.total_quantity(), 200);

  level.reduce_quantity(50);
  EXPECT_EQ(level.total_quantity(), 150);
}

TEST_F(PriceLevelTest, MixedQuantities) {
  PriceLevel level;
  orders_[0].quantity = 50;
  orders_[1].quantity = 150;
  orders_[2].quantity = 75;

  level.push_back(&orders_[0]);
  level.push_back(&orders_[1]);
  level.push_back(&orders_[2]);

  EXPECT_EQ(level.total_quantity(), 275);

  level.remove(&orders_[1]);
  EXPECT_EQ(level.total_quantity(), 125);
}
