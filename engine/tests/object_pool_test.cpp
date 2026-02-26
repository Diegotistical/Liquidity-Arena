/// @file object_pool_test.cpp
/// @brief Unit tests for the ObjectPool template.

#include "object_pool.hpp"
#include "order.hpp"
#include <gtest/gtest.h>


using namespace arena;

TEST(ObjectPoolTest, InitialState) {
  ObjectPool<Order, 100> pool;
  EXPECT_EQ(pool.capacity(), 100u);
  EXPECT_EQ(pool.allocated(), 0u);
  EXPECT_EQ(pool.available(), 100u);
  EXPECT_FALSE(pool.full());
}

TEST(ObjectPoolTest, AllocateAndDeallocate) {
  ObjectPool<Order, 10> pool;

  Order *o1 = pool.allocate();
  ASSERT_NE(o1, nullptr);
  EXPECT_EQ(pool.allocated(), 1u);
  EXPECT_EQ(pool.available(), 9u);

  Order *o2 = pool.allocate();
  ASSERT_NE(o2, nullptr);
  EXPECT_NE(o1, o2); // Different addresses
  EXPECT_EQ(pool.allocated(), 2u);

  pool.deallocate(o1);
  EXPECT_EQ(pool.allocated(), 1u);
  EXPECT_EQ(pool.available(), 9u);

  pool.deallocate(o2);
  EXPECT_EQ(pool.allocated(), 0u);
  EXPECT_EQ(pool.available(), 10u);
}

TEST(ObjectPoolTest, ExhaustPool) {
  constexpr std::size_t N = 5;
  ObjectPool<Order, N> pool;

  Order *orders[N];
  for (std::size_t i = 0; i < N; ++i) {
    orders[i] = pool.allocate();
    ASSERT_NE(orders[i], nullptr);
  }

  EXPECT_TRUE(pool.full());
  EXPECT_EQ(pool.allocate(), nullptr); // Should return nullptr when exhausted

  // Deallocate one, allocate again.
  pool.deallocate(orders[0]);
  EXPECT_FALSE(pool.full());

  Order *reused = pool.allocate();
  ASSERT_NE(reused, nullptr);
  EXPECT_TRUE(pool.full());
}

TEST(ObjectPoolTest, ReuseAfterDealloc) {
  ObjectPool<Order, 3> pool;

  Order *a = pool.allocate();
  pool.deallocate(a);

  Order *b = pool.allocate();
  // After dealloc + alloc, the same slot should be reused.
  EXPECT_EQ(a, b);
}

TEST(ObjectPoolTest, OwnershipCheck) {
  ObjectPool<Order, 10> pool;
  Order *o = pool.allocate();
  EXPECT_TRUE(pool.owns(o));

  Order stack_order;
  EXPECT_FALSE(pool.owns(&stack_order));

  pool.deallocate(o);
}

TEST(ObjectPoolTest, AllocatedObjectIsDefaultConstructed) {
  ObjectPool<Order, 10> pool;
  Order *o = pool.allocate();
  EXPECT_EQ(o->id, INVALID_ORDER);
  EXPECT_EQ(o->price, INVALID_PRICE);
  EXPECT_EQ(o->quantity, 0);
  EXPECT_EQ(o->prev, nullptr);
  EXPECT_EQ(o->next, nullptr);
  pool.deallocate(o);
}
