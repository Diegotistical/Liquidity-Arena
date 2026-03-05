/// @file tcp_server.cpp
/// @brief Cross-platform TCP server implementation.
///
/// Uses Winsock2 on Windows, POSIX sockets on Linux.
/// Handles NewOrder, Cancel, and Modify messages,
/// sends proper Reject messages with reason codes.

#include "tcp_server.hpp"

#include <cstring>
#include <sstream>


namespace arena {

TcpServer::TcpServer(MatchingEngine& engine, uint16_t port) : engine_(engine), port_(port) {}

TcpServer::~TcpServer() {
  stop();
}

bool TcpServer::start() {
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    log("WSAStartup failed");
    return false;
  }
#endif

  listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock_ == INVALID_SOCK) {
    log("Failed to create socket");
    return false;
  }

  // Allow port reuse.
  int opt = 1;
  setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt),
             sizeof(opt));

  // Set non-blocking mode.
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(listen_sock_, FIONBIO, &mode);
#endif

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(listen_sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    log("Bind failed on port " + std::to_string(port_));
    return false;
  }

  if (listen(listen_sock_, SOMAXCONN) != 0) {
    log("Listen failed");
    return false;
  }

  running_ = true;
  log("Engine listening on port " + std::to_string(port_));
  return true;
}

bool TcpServer::poll(int timeout_ms) {
  if (!running_)
    return false;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(listen_sock_, &read_fds);

  SocketType max_fd = listen_sock_;
  for (const auto& client : clients_) {
    if (client.sock != INVALID_SOCK) {
      FD_SET(client.sock, &read_fds);
      if (client.sock > max_fd)
        max_fd = client.sock;
    }
  }

  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int result = select(static_cast<int>(max_fd + 1), &read_fds, nullptr, nullptr, &tv);
  if (result < 0) {
    log("select() error");
    return false;
  }
  if (result == 0)
    return true; // Timeout, no activity

  // Check for new connections.
  if (FD_ISSET(listen_sock_, &read_fds)) {
    accept_client();
  }

  // Check each client for data.
  for (std::size_t i = 0; i < clients_.size(); ++i) {
    if (clients_[i].sock != INVALID_SOCK && FD_ISSET(clients_[i].sock, &read_fds)) {
      handle_client(i);
    }
  }

  return true;
}

void TcpServer::stop() {
  running_ = false;
  for (auto& client : clients_) {
    if (client.sock != INVALID_SOCK) {
#ifdef _WIN32
      closesocket(client.sock);
#else
      close(client.sock);
#endif
      client.sock = INVALID_SOCK;
    }
  }
  clients_.clear();

  if (listen_sock_ != INVALID_SOCK) {
#ifdef _WIN32
    closesocket(listen_sock_);
    WSACleanup();
#else
    close(listen_sock_);
#endif
    listen_sock_ = INVALID_SOCK;
  }
}

void TcpServer::accept_client() {
  sockaddr_in client_addr{};
  int addr_len = sizeof(client_addr);
  SocketType client_sock = accept(listen_sock_, reinterpret_cast<sockaddr *>(&client_addr),
#ifdef _WIN32
                                  &addr_len);
#else
                                  reinterpret_cast<socklen_t *>(&addr_len));
#endif

  if (client_sock == INVALID_SOCK)
    return;

  // Set non-blocking.
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(client_sock, FIONBIO, &mode);
#endif

  ClientConn conn;
  conn.sock = client_sock;
  conn.recv_buffer.resize(4096);
  conn.recv_offset = 0;
  clients_.push_back(std::move(conn));

  log("Client connected (total: " + std::to_string(clients_.size()) + ")");
}

void TcpServer::handle_client(std::size_t idx) {
  auto& client = clients_[idx];

  // Read into buffer.
  int bytes_read =
      recv(client.sock, reinterpret_cast<char *>(client.recv_buffer.data() + client.recv_offset),
           static_cast<int>(client.recv_buffer.size() - client.recv_offset), 0);

  if (bytes_read <= 0) {
    remove_client(idx);
    return;
  }

  client.recv_offset += static_cast<std::size_t>(bytes_read);

  // Process complete messages.
  std::size_t pos = 0;
  while (pos + sizeof(MsgHeader) <= client.recv_offset) {
    MsgHeader header;
    std::memcpy(&header, client.recv_buffer.data() + pos, sizeof(MsgHeader));

    std::size_t total_msg_size = sizeof(MsgHeader) + header.length;
    if (pos + total_msg_size > client.recv_offset)
      break; // Incomplete message

    // Process the message.
    const uint8_t *payload = client.recv_buffer.data() + pos + sizeof(MsgHeader);
    process_message(idx, header.type, payload, header.length);

    pos += total_msg_size;
  }

  // Compact the buffer (move unprocessed bytes to front).
  if (pos > 0) {
    std::size_t remaining = client.recv_offset - pos;
    if (remaining > 0) {
      std::memmove(client.recv_buffer.data(), client.recv_buffer.data() + pos, remaining);
    }
    client.recv_offset = remaining;
  }
}

void TcpServer::remove_client(std::size_t idx) {
  if (clients_[idx].sock != INVALID_SOCK) {
#ifdef _WIN32
    closesocket(clients_[idx].sock);
#else
    close(clients_[idx].sock);
#endif
  }
  clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(idx));
  log("Client disconnected (total: " + std::to_string(clients_.size()) + ")");
}

void TcpServer::process_message(std::size_t client_idx, MsgType type, const uint8_t *payload,
                                uint32_t len) {
  switch (type) {
  case MsgType::NEW_ORDER: {
    if (len < sizeof(NewOrderMsg)) {
      send_reject(client_idx, 0, RejectReason::INVALID_PARAMS);
      return;
    }
    auto msg = deserialize<NewOrderMsg>(payload);

    // Validate order parameters.
    if (msg.quantity <= 0) {
      send_reject(client_idx, msg.id, RejectReason::INVALID_PARAMS);
      return;
    }
    if (msg.type == OrderType::LIMIT && msg.price <= 0) {
      send_reject(client_idx, msg.id, RejectReason::INVALID_PARAMS);
      return;
    }

    Order *result = engine_.process_new_order(msg.id, msg.side, msg.type, msg.price, msg.quantity,
                                              0, msg.display_qty);

    if (result == nullptr && msg.type != OrderType::MARKET) {
      // Could be pool exhausted or fully filled — only reject if pool issue.
      // (A fully-filled crossing order returns nullptr legitimately.)
    }

    // Broadcast updated book snapshot to all clients.
    BookUpdateMsg snapshot = engine_.get_book_snapshot();
    broadcast(snapshot);
    break;
  }
  case MsgType::CANCEL_ORDER: {
    if (len < sizeof(CancelOrderMsg)) {
      send_reject(client_idx, 0, RejectReason::INVALID_PARAMS);
      return;
    }
    auto msg = deserialize<CancelOrderMsg>(payload);
    bool ok = engine_.process_cancel(msg.id);
    if (!ok) {
      send_reject(client_idx, msg.id, RejectReason::ORDER_NOT_FOUND);
    }

    BookUpdateMsg snapshot = engine_.get_book_snapshot();
    broadcast(snapshot);
    break;
  }
  case MsgType::MODIFY_ORDER: {
    if (len < sizeof(ModifyOrderMsg)) {
      send_reject(client_idx, 0, RejectReason::INVALID_PARAMS);
      return;
    }
    auto msg = deserialize<ModifyOrderMsg>(payload);

    if (msg.new_quantity <= 0 || msg.new_price <= 0) {
      send_reject(client_idx, msg.id, RejectReason::INVALID_PARAMS);
      return;
    }

    bool ok = engine_.process_modify(msg.id, msg.new_price, msg.new_quantity);
    if (!ok) {
      send_reject(client_idx, msg.id, RejectReason::INVALID_MODIFY);
    }

    BookUpdateMsg snapshot = engine_.get_book_snapshot();
    broadcast(snapshot);
    break;
  }
  default:
    break;
  }
}

void TcpServer::send_reject(std::size_t client_idx, OrderId id, RejectReason reason) {
  if (client_idx >= clients_.size())
    return;
  if (clients_[client_idx].sock == INVALID_SOCK)
    return;

  RejectMsg reject;
  reject.id = id;
  reject.reason = reason;

  uint8_t buf[MAX_MSG_SIZE];
  std::size_t total = serialize(reject, buf);
  send(clients_[client_idx].sock, reinterpret_cast<const char *>(buf), static_cast<int>(total), 0);
}

void TcpServer::log(const std::string& msg) {
  if (on_log_) {
    on_log_(msg);
  }
}

} // namespace arena
