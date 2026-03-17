/// @file main.cpp
/// @brief Entry point for the Liquidity Arena matching engine.
///
/// Creates the core components and starts the TCP server event loop.
/// The engine accepts connections from Python simulation clients.

#include "matching_engine.hpp"
#include "order_book.hpp"
#include "tcp_server.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-reference"
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {
std::atomic<bool> g_running{true};
}

void signal_handler(int /*signum*/) {
  g_running.store(false);
}

void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " [OPTIONS]\n"
            << "  --port <PORT>   TCP port (default: 9876, range: 1024-65535)\n"
            << "  --version       Show version info\n"
            << "  --help          Show this help\n";
}

int main(int argc, char *argv[]) {
  uint16_t port = 9876;

  // Parse arguments.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::cout << "Liquidity Arena v" << ARENA_VERSION << "\n";
      return 0;
    }
    if (std::strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      int p = std::atoi(argv[++i]);
      if (p < 1024 || p > 65535) {
        std::cerr << "Error: port must be in range 1024-65535\n";
        return 1;
      }
      port = static_cast<uint16_t>(p);
    }
  }

  spdlog::init_thread_pool(8192, 1);
  auto async_logger = spdlog::stdout_color_mt<spdlog::async_factory>("async_logger");
  spdlog::set_default_logger(async_logger);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  spdlog::info(R"(
   _     _             _     _ _ _            _
  | |   (_)           (_)   | (_) |          / \   _ __ ___ _ __   __ _
  | |    _  __ _ _   _ _  __| |_| |_ _   _ / _ \ | '__/ _ \ '_ \ / _` |
  | |___| |/ _` | | | | |/ _` | | __| | | / ___ \| | |  __/ | | | (_| |
  |_____|_|\__, |_|_|_|_|\__,_|_|\__|\__, /_/   \_\_|  \___|_| |_|\__,_|
              |_|   |_|              |___/
    )");

  spdlog::info("=== Liquidity Arena Matching Engine v{} ===", ARENA_VERSION);
  spdlog::info("  C++20 | Integer Ticks | Object Pool | FIFO P-T-P");
  spdlog::info("  Pool capacity: {} orders", arena::MAX_ORDERS);

  // Register signal handler for graceful shutdown.
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Create core components.
  arena::OrderBook book;
  arena::MatchingEngine engine(book);
  arena::TcpServer server(engine, port);

  // Set up fill logging.
  engine.set_fill_callback([](const arena::FillMsg& fill) {
    spdlog::info("[FILL] maker={} taker={} price={} qty={}", 
                 fill.maker_id, fill.taker_id, fill.price, fill.quantity);
  });

  // Set up server logging.
  server.set_log_callback([](const std::string& msg) { spdlog::info("[SERVER] {}", msg); });

  if (!server.start()) {
    spdlog::error("Failed to start server on port {}", port);
    return 1;
  }

  // Start the background matching engine thread
  engine.start();

  // Main event loop.
  spdlog::info("[ENGINE] Running on port {}. Press Ctrl+C to stop.", port);
  while (g_running.load()) {
    if (!server.poll(10)) // Reduced polling time to process queues faster
      break;
  }

  engine.stop();
  server.stop();
  spdlog::info("[ENGINE] Shutdown complete.");
  spdlog::info("  Total orders:  {}", engine.total_orders());
  spdlog::info("  Total fills:   {}", engine.total_fills());
  spdlog::info("  Total cancels: {}", engine.total_cancels());
  spdlog::info("  Pool usage:    {}/{}", (arena::MAX_ORDERS - book.pool_available()), arena::MAX_ORDERS);

  spdlog::shutdown();

  return 0;
}
