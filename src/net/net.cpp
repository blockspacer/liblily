#include "net.h"

namespace lily {
  Address::Address(lily::AddressFamily family) {
    if (family == AddressFamily::V4) {
      m_base = std::unique_ptr<sockaddr>(reinterpret_cast<sockaddr *>(new sockaddr_in{}));
    } else if (family == AddressFamily::V6) {
      m_base = std::unique_ptr<sockaddr>(reinterpret_cast<sockaddr *>(new sockaddr_in6{}));
    } else if (family == AddressFamily::UnixSock) {
      m_base = std::unique_ptr<sockaddr>(reinterpret_cast<sockaddr *>(new sockaddr_un{}));
    } else {
      fprintf(stderr, "unknown address family: %d\n", family);
      return;
    }
    m_base->sa_family = family;
  }
  Address::Address(const char *path) : m_base((sockaddr *) (new sockaddr_un{})) {
    auto len = strlen(path);
    if (len > sizeof(std::declval<sockaddr_un>().sun_path)) {
      fprintf(stderr, "Length Error: length of path should be less than 108.\n");
    }
    UnixSock()->sun_family = AddressFamily::UnixSock;
    memcpy(UnixSock()->sun_path, path, len);
  }
  Address::Address(const char *ip, uint16_t port) {
    if (IsIPv4(ip)) {
      m_base = std::unique_ptr<sockaddr>(reinterpret_cast<sockaddr *>(new sockaddr_in{}));
      int err = inet_pton(AddressFamily::V4, ip, &V4()->sin_addr);
      if (err != 1) perror("inet_pton");
      V4()->sin_family = AddressFamily::V4;
      V4()->sin_port = htons(port);
    } else {
      m_base = std::unique_ptr<sockaddr>(reinterpret_cast<sockaddr *>(new sockaddr_in6{}));
      int err = inet_pton(AddressFamily::V6, ip, &V6()->sin6_addr);
      if (err != 1) perror("inet_pton");
      V6()->sin6_family = AddressFamily::V6;
      V6()->sin6_port = htons(port);
    }
  }
  size_t Address::AddressSize() const {
    switch (m_base->sa_family) {
      case AddressFamily::V4:return sizeof(sockaddr_in);
      case AddressFamily::V6:return sizeof(sockaddr_in6);
      case AddressFamily::UnixSock:return sizeof(sockaddr_un);
      default:return size_t(-1);
    }
  }
  std::string Address::GetIPString() const {
    if (m_base->sa_family == AddressFamily::V4) {
      char buf[INET_ADDRSTRLEN];
      if (inet_ntop(m_base->sa_family, &V4()->sin_addr, buf, AddressSize()) == nullptr) {
        perror("inet_ntop");
        return {};
      }
      return buf;
    } else if (m_base->sa_family == AddressFamily::V6) {
      char buf[INET6_ADDRSTRLEN];
      if (inet_ntop(m_base->sa_family, &V6()->sin6_addr, buf, AddressSize()) == nullptr) {
        perror("inet_ntop");
        return {};
      }
      return buf;
    } else if (m_base->sa_family == AddressFamily::UnixSock) {
      return std::string(UnixSock()->sun_path);
    } else {
      fprintf(stderr, "unknown address family: %d.\n", m_base->sa_family);
      return {};
    }
  }
  uint16_t Address::GetPort() const {
    switch (m_base->sa_family) {
      case AddressFamily::V4:return ntohs(V4()->sin_port);
      case AddressFamily::V6:return ntohs(V6()->sin6_port);
      default:return -1;
    }
  }
  std::string Address::String() const {
    switch (m_base->sa_family) {
      case AddressFamily::V4: {
        return GetIPString() + ':' + GetPortString();
      }
      case AddressFamily::V6: {
        return '[' + GetIPString() + "]:" + GetPortString();
      }
      default: {
        return GetIPString();
      }
    }
  }
  Socket &Socket::operator=(lily::Socket &&other) {
    if (&other == this) return *this;
    if (m_fd >= 0) close(m_fd);
    m_fd = other.m_fd, other.m_fd = -1;
    m_local = std::move(other.m_local);
    m_peer = std::move(other.m_peer);
    return *this;
  }
  Socket::Socket(lily::Socket &&other) noexcept :
      m_fd(other.m_fd),
      m_local(std::move(other.m_local)),
      m_peer(std::move(other.m_peer)) { other.m_fd = -1; }
  R<Ref<Client>, Error> Client::Connect(lily::NetProtocol proto, const char *path) {
    auto conn = New<Client>();
    conn->m_sock = Socket(AddressFamily::UnixSock, proto, 0);
    conn->m_sock.m_peer = Address(path);
    conn->m_sock.m_local = Address(conn->m_sock.m_peer.GetFamily());
    int err = connect(conn->m_sock.m_fd, conn->m_sock.m_peer.Base(), conn->m_sock.m_peer.AddressSize());
    if (err != 0) {
      return {nullptr, ERRNO};
    }
    socklen_t len = conn->m_sock.m_local.AddressSize();
    err = getsockname(conn->m_sock.m_fd, conn->m_sock.m_local.Base(), &len);
    if (err != 0) {
      return {nullptr, ERRNO};
    }
    return {conn, NoError};
  }
  R<Ref<Client>, Error> Client::Connect(lily::NetProtocol proto, const char *ip, uint16_t port) {
    auto conn = New<Client>();
    conn->m_sock = Socket(GetFamily(ip), proto, 0);
    conn->m_sock.m_peer = Address(ip, port);
    conn->m_sock.m_local = Address(conn->m_sock.m_peer.GetFamily());
    int err = connect(conn->m_sock.m_fd, conn->m_sock.m_peer.Base(), conn->m_sock.m_peer.AddressSize());
    if (err != 0) {
      return {nullptr, ERRNO};
    }
    socklen_t len = conn->m_sock.m_local.AddressSize();
    err = getsockname(conn->m_sock.m_fd, conn->m_sock.m_local.Base(), &len);
    if (err != 0) {
      return {nullptr, ERRNO};
    }
    return {conn, NoError};
  }
  Ref<Client> Client::FromSocket(lily::Socket &&sock) {
    auto conn = New<Client>();
    conn->m_sock = std::move(sock);
    return conn;
  }
  R<ssize_t, Error> Client::Read(lily::span<char> buf) {
    auto c = read(m_sock.m_fd, buf.as_writable_chars(), buf.size());
    if (c < 0) { return {c, ERRNO}; }
    return {c, NoError};
  }
  R<ssize_t, Error> Client::Write(lily::span<char> buf) {
    auto c = write(m_sock.m_fd, buf.as_chars(), buf.size());
    if (c < 0) { return {c, ERRNO}; }
    return {c, NoError};
  }
  Error Client::SetReadTimeout(int usec) {
    struct timeval tv[1];
    tv->tv_sec = usec / 1000000;
    tv->tv_usec = usec % 1000000;
    if (setsockopt(m_sock.m_fd, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(timeval) < 0)) {
      return ERRNO;
    }
    return NoError;
  }
  Error Client::SetWriteTimeout(int usec) {
    struct timeval tv[1];
    tv->tv_sec = usec / 1000000;
    tv->tv_usec = usec % 1000000;
    if (setsockopt(m_sock.m_fd, SOL_SOCKET, SO_SNDTIMEO, tv, sizeof(timeval)) < 0) {
      return ERRNO;
    }
    return NoError;
  }
  R<Ref<TCPServer>, Error> TCPServer::ListenTCP(const char *path) {
    auto listener = std::make_shared<TCPServer>();
    listener->m_sock = Socket(AddressFamily::UnixSock, NetProtocol::TCP, 0);
    listener->m_sock.m_local = Address(path);
    unlink(path);
    if (bind(listener->m_sock.m_fd, listener->m_sock.m_local.Base(), listener->m_sock.m_local.AddressSize()) < 0) {
      return {nullptr, ERRNO};
    }
    if (listen(listener->m_sock.m_fd, TCPServer::Backlog) < 0) {
      return {nullptr, ERRNO};
    }
    return {listener, NoError};
  }
  R<Ref<TCPServer>, Error> TCPServer::ListenTCP(const char *ip, uint16_t port) {
    auto listener = std::make_shared<TCPServer>();
    listener->m_sock = Socket(GetFamily(ip), NetProtocol::TCP, 0);
    listener->m_sock.m_local = Address(ip, port);
    if (bind(listener->m_sock.m_fd, listener->m_sock.m_local.Base(), listener->m_sock.m_local.AddressSize()) < 0) {
      return {nullptr, ERRNO};
    }
    if (listen(listener->m_sock.m_fd, TCPServer::Backlog) < 0) {
      return {nullptr, ERRNO};
    }
    return {listener, NoError};
  }
  R<Ref<Client>, Error> TCPServer::Accept() {
    Address remote(m_sock.m_local.GetFamily());
    socklen_t len = m_sock.m_local.AddressSize();
    int fd = accept(m_sock.m_fd, remote.Base(), &len);
    if (fd < 0) {
      // perror("accept");
      return {nullptr, ERRNO};
    }
    Socket peer(fd);
    peer.m_peer = std::move(remote);
    peer.m_local = Address(peer.m_peer.GetFamily());
    int err = getsockname(fd, peer.m_local.Base(), &len);
    if (err != 0) {
      return {Client::FromSocket(std::move(peer)), ERRNO};
    }
    return {Client::FromSocket(std::move(peer)), NoError};
  }
  int TCPServer::Backlog = 128;
  UDPServer::UDPServer(const char *ip, uint16_t port) : m_sock(GetFamily(ip), NetProtocol::UDP, 0) {
    m_sock.m_local = Address(ip, port);
    int err = bind(m_sock.m_fd, m_sock.m_local.Base(), m_sock.m_local.AddressSize());
    if (err != 0) {
      perror("bind");
    }
  }
  R<ssize_t, Error> UDPServer::Read(span<char> buf, lily::Address &addr) {
    addr = Address(m_sock.m_local.GetFamily());
    socklen_t len = addr.AddressSize();
    int c = recvfrom(m_sock.m_fd, buf.as_writable_chars(), buf.size(), 0, addr.Base(), &len);
    if (c < 0) {
      return {c, ERRNO};
    }
    return {c, NoError};
  }
  R<ssize_t, Error> UDPServer::Write(span<char> buf, const lily::Address &addr) {
    int c = sendto(m_sock.m_fd, buf.as_chars(), buf.size(), 0, addr.Base(), addr.AddressSize());
    if (c < 0) {
      return {c, ERRNO};
    }
    return {c, NoError};
  }
  Error UDPServer::SetReadTimeout(int usec) {
    struct timeval tv[1];
    tv->tv_sec = usec / 1000000;
    tv->tv_usec = usec % 1000000;
    if (setsockopt(m_sock.m_fd, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(timeval)) < 0) { return ERRNO; }
    return NoError;
  }
  Error UDPServer::SetWriteTimeout(int usec) {
    struct timeval tv[1];
    tv->tv_sec = usec / 1000000;
    tv->tv_usec = usec % 1000000;
    if (setsockopt(m_sock.m_fd, SOL_SOCKET, SO_SNDTIMEO, tv, sizeof(timeval)) < 0) { return ERRNO; }
    return NoError;
  }
}