/// @file latency_bench.cpp
/// @brief Google Benchmark latency tests for the matching engine hot path.
///
/// Measures per-operation latency for:
///   - add_order (non-crossing limit)
///   - cancel_order
///   - crossing limit match (single fill)
///   - market order match

#include "matching_engine.hpp"
#include "order_book.hpp"
#include <benchmark/benchmark.h>

using namespace arena;

/// Benchmark: Adding a non-crossing limit order.
static void BM_AddOrder(benchmark::State &state) {
  OrderBook book;
  MatchingEngine engine(book);
  OrderId id = 1;

  for (auto _ : state) {
    // Add a bid that won't cross (no asks in the book).
    Tick price = 10000 - static_cast<Tick>(id % 100);
    engine.process_new_order(id, Side::BID, OrderType::LIMIT, price, 100, 0);
    ++id;
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_AddOrder);

/// Benchmark: Cancelling an order.
static void BM_CancelOrder(benchmark::State &state) {
  OrderBook book;
  MatchingEngine engine(book);

  // Pre-fill the book with orders.
  constexpr int N = 100000;
  for (int i = 1; i <= N; ++i) {
    engine.process_new_order(static_cast<OrderId>(i), Side::BID,
                             OrderType::LIMIT, 10000 - (i % 100), 100, 0);
  }

  OrderId cancel_id = 1;
  for (auto _ : state) {
    engine.process_cancel(cancel_id);
    ++cancel_id;
    if (cancel_id > static_cast<OrderId>(N)) {
      state.PauseTiming();
      // Refill
      cancel_id = 1;
      for (int i = 1; i <= N; ++i) {
        engine.process_new_order(static_cast<OrderId>(i), Side::BID,
                                 OrderType::LIMIT, 10000 - (i % 100), 100, 0);
      }
      state.ResumeTiming();
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CancelOrder);

/// Benchmark: Crossing limit order (causes a single fill).
static void BM_CrossingMatch(benchmark::State &state) {
  OrderBook book;
  MatchingEngine engine(book);
  OrderId id = 1;

  for (auto _ : state) {
    state.PauseTiming();
    // Place a resting ask.
    engine.process_new_order(id, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
    ++id;
    state.ResumeTiming();

    // Cross it with a bid — this is the operation we're measuring.
    engine.process_new_order(id, Side::BID, OrderType::LIMIT, 10000, 100, 0);
    ++id;
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CrossingMatch);

/// Benchmark: Market order that fills against one resting order.
static void BM_MarketOrder(benchmark::State &state) {
  OrderBook book;
  MatchingEngine engine(book);
  OrderId id = 1;

  for (auto _ : state) {
    state.PauseTiming();
    engine.process_new_order(id, Side::ASK, OrderType::LIMIT, 10000, 100, 0);
    ++id;
    state.ResumeTiming();

    engine.process_new_order(id, Side::BID, OrderType::MARKET, 0, 100, 0);
    ++id;
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_MarketOrder);
