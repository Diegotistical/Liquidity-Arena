#pragma once
/// @file tcp_server.hpp
/// @brief Cross-platform TCP server for the matching engine.
///
/// Uses Winsock2 on Windows, POSIX sockets on Linux/macOS.
/// Single-threaded event loop with select().
/// Accepts multiple client connections, reads NewOrderMsg/CancelMsg/ModifyMsg,
/// feeds to MatchingEngine, broadcasts Fill/BookUpdate to all clients.
///
/// Framing: [1-byte MsgType] [4-byte payload length (LE)] [payload]

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
constexpr SocketType INVALID_SOCK = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
using SocketType = int;
constexpr SocketType INVALID_SOCK = -1;
#endif

#include "matching_engine.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace arena {

/// Callback for log messages.
using LogCallback = std::function<void(const std::string&)>;

class TcpServer {
public:
  /// @param engine Reference to the matching engine.
  /// @param port   TCP port to listen on.
  explicit TcpServer(MatchingEngine& engine, uint16_t port = 9876);
  ~TcpServer();

  // Non-copyable.
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  /// Set a logging callback.
  void set_log_callback(LogCallback cb) { on_log_ = std::move(cb); }

  /// Initialize the server socket. Returns true on success.
  bool start();

  /// Run one iteration of the event loop (select + process).
  /// Call this in a loop. Returns false if the server should shut down.
  bool poll(int timeout_ms = 100);

  /// Shut down the server and close all connections.
  void stop();

  /// Broadcast a message to all connected clients.
  template <typename MsgT>
  void broadcast(const MsgT& msg);

  [[nodiscard]] std::size_t client_count() const noexcept { return clients_.size(); }

private:
  void accept_client();
  void handle_client_read(std::size_t idx);
  void handle_client_write(std::size_t idx);
  void remove_client(std::size_t idx);
  void process_message(std::size_t client_idx, MsgType type, const uint8_t *payload, uint32_t len);

  /// Send a reject message to a specific client.
  void send_reject(std::size_t client_idx, OrderId id, RejectReason reason);

  /// Process broadcast events and rejections from the matching engine.
  void consume_engine_events();

  void log(const std::string& msg);

  MatchingEngine& engine_;
  uint16_t port_;
  SocketType listen_sock_ = INVALID_SOCK;
  bool running_ = false;

  struct ClientConn {
    SocketType sock = INVALID_SOCK;
    std::vector<uint8_t> recv_buffer;
    std::size_t recv_offset = 0;
    std::vector<uint8_t> send_buffer;
  };

  std::vector<ClientConn> clients_;
  LogCallback on_log_;

  // Reusable send buffer to avoid allocation per broadcast.
  uint8_t send_buffer_[MAX_MSG_SIZE * 2]{};
};

// ── Template implementations ─────────────────────────────────────────

template <typename MsgT>
void TcpServer::broadcast(const MsgT& msg) {
  std::size_t total = serialize(msg, send_buffer_);
  for (auto& client : clients_) {
    if (client.sock != INVALID_SOCK) {
      client.send_buffer.insert(client.send_buffer.end(), send_buffer_, send_buffer_ + total);
    }
  }
}

} // namespace arena
