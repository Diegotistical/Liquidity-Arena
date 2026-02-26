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

  std::cout << R"(
   _     _             _     _ _ _            _
  | |   (_)           (_)   | (_) |          / \   _ __ ___ _ __   __ _
  | |    _  __ _ _   _ _  __| |_| |_ _   _ / _ \ | '__/ _ \ '_ \ / _` |
  | |___| |/ _` | | | | |/ _` | | __| | | / ___ \| | |  __/ | | | (_| |
  |_____|_|\__, |_|_|_|_|\__,_|_|\__|\__, /_/   \_\_|  \___|_| |_|\__,_|
              |_|   |_|              |___/
    )" << '\n';

  std::cout << "=== Liquidity Arena Matching Engine v" << ARENA_VERSION << " ===\n";
  std::cout << "  C++20 | Integer Ticks | Object Pool | FIFO P-T-P\n";
  std::cout << "  Pool capacity: " << arena::MAX_ORDERS << " orders\n\n";

  // Register signal handler for graceful shutdown.
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Create core components.
  arena::OrderBook book;
  arena::MatchingEngine engine(book);
  arena::TcpServer server(engine, port);

  // Set up fill logging.
  engine.set_fill_callback([](const arena::FillMsg& fill) {
    std::cout << "[FILL] maker=" << fill.maker_id << " taker=" << fill.taker_id
              << " price=" << fill.price << " qty=" << fill.quantity << '\n';
  });

  // Set up server logging.
  server.set_log_callback([](const std::string& msg) { std::cout << "[SERVER] " << msg << '\n'; });

  if (!server.start()) {
    std::cerr << "Failed to start server on port " << port << '\n';
    return 1;
  }

  // Main event loop.
  std::cout << "[ENGINE] Running on port " << port << ". Press Ctrl+C to stop.\n\n";
  while (g_running.load()) {
    if (!server.poll(100))
      break;
  }

  server.stop();
  std::cout << "\n[ENGINE] Shutdown complete.\n";
  std::cout << "  Total orders:  " << engine.total_orders() << '\n';
  std::cout << "  Total fills:   " << engine.total_fills() << '\n';
  std::cout << "  Total cancels: " << engine.total_cancels() << '\n';
  std::cout << "  Pool usage:    " << (arena::MAX_ORDERS - book.pool_available()) << "/"
            << arena::MAX_ORDERS << '\n';

  return 0;
}
