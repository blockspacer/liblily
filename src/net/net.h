#ifndef LIBLILY_SRC_NET_NET_H_
#define LIBLILY_SRC_NET_NET_H_
#include <memory>
#include <iostream>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include "../common/slice.h"
#include "../common/common.h"
namespace lily {
  // 一个对TCP/UDP客户端/服务端的简单封装
  enum AddressFamily {
    V4 = AF_INET, V6 = AF_INET6, UnixSock = AF_UNIX
  };
  enum NetProtocol {
    TCP = SOCK_STREAM, UDP = SOCK_DGRAM
  };
  inline bool IsIPv4(const char *ip) {
    while ((*ip) != '\0') { if ((*ip++) == ':')return false; }
    return true;
  }
  inline AddressFamily GetFamily(const char *ip) {
    while ((*ip) != '\0') { if ((*ip++) == ':')return AddressFamily::V6; }
    return AddressFamily::V4;
  }
  // 地址类，对sockaddr族的简单封装
  class Address {
   private:
    friend class Socket;
   private:
    std::unique_ptr<sockaddr> m_base = nullptr;
   private:
    sockaddr_in *V4() { return reinterpret_cast<sockaddr_in *>(Base()); }
    sockaddr_in6 *V6() { return reinterpret_cast<sockaddr_in6 *>(Base()); }
    sockaddr_un *UnixSock() { return reinterpret_cast<sockaddr_un *>(Base()); }
    const sockaddr_in *V4() const { return reinterpret_cast<const sockaddr_in *>(Base()); }
    const sockaddr_in6 *V6() const { return reinterpret_cast<const sockaddr_in6 *>(Base()); }
    const sockaddr_un *UnixSock() const { return reinterpret_cast<const sockaddr_un *>(Base()); }
   public:
    Address() = default;
    explicit Address(AddressFamily family);
    explicit Address(const char *path);
    Address(const char *ip, uint16_t port);
    sockaddr *Base() { return m_base.get(); }
    const sockaddr *Base() const { return m_base.get(); }
    size_t AddressSize() const;
    AddressFamily GetFamily() const { return static_cast<AddressFamily>(m_base->sa_family); }
    std::string GetIPString() const;
    uint16_t GetPort() const;
    std::string GetPortString() const { return std::to_string(GetPort()); }
    std::string String() const;
  };

  // Socket类，对socket的简单封装
  class Socket {
    friend class Client;
    friend class TCPServer;
    friend class UDPServer;
   private:
    int m_fd;
    Address m_local;
    Address m_peer;
   public:
    Socket() : m_fd(-1) {}
    ~Socket() { close(m_fd); }
    Socket(int domain, int type, int protocol) : m_fd(socket(domain, type, protocol)) {}
    explicit Socket(int fd) : m_fd(fd) {}
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket &operator=(Socket &&);
    Socket(Socket &&other) noexcept;
    int GetFd() const { return m_fd; }
  };

  // TCP/UDP客户端
  class Client {
   private:
    Socket m_sock;
   public:
    Client() = default;
    Client(NetProtocol proto, const char *ip, uint16_t port);
    static R<Ref<Client>, Error> Connect(NetProtocol proto, const char *path);
    static R<Ref<Client>, Error> Connect(NetProtocol proto, const char *ip, uint16_t port);
    static Ref<Client> FromSocket(Socket &&sock);
    R<ssize_t, Error> Read(span<char> buf);
    R<ssize_t, Error> Write(span<char> buf);
    const Address &LocalAddr() const { return m_sock.m_local; }
    const Address &PeerAddr() const { return m_sock.m_peer; }
    Error SetReadTimeout(int usec);
    Error SetWriteTimeout(int usec);
  };

  // TCP服务端
  class TCPServer {
   public:
    static int Backlog;
   private:
    Socket m_sock;
   public:
    static R<Ref<TCPServer>, Error> ListenTCP(const char *path);
    static R<Ref<TCPServer>, Error> ListenTCP(const char *ip, uint16_t port);
    R<Ref<Client>, Error> Accept();
  };
  // UDP服务端
  class UDPServer {
   private:
    Socket m_sock;
   public:
    UDPServer(const char *ip, uint16_t port);
    R<ssize_t, Error> Read(span<char> buf, Address &addr);
    R<ssize_t, Error> Write(span<char> buf, const Address &addr);
    const Address &LocalAddr() const { return m_sock.m_local; }
    Error SetReadTimeout(int usec);
    Error SetWriteTimeout(int usec);
  };
}

#endif //LIBLILY_SRC_NET_NET_H_
