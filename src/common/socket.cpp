//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/socket.h>
#include <common/define.h>

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

#ifdef PLATFORM_LINUX
    #include <unistd.h>
    #include <netinet/tcp.h>
#endif

using namespace Dream;
using namespace Dream::detail;

#ifdef PLATFORM_WINDOWS
namespace {
    class WinsockRuntime {
    public:
        WinsockRuntime() {
            WSADATA data{};
            const int error = WSAStartup(MAKEWORD(2, 2), &data);
            if (error != 0) {
                LOG_FATAL("WSAStartup error: {}", Socket::errorString(error));
            }
        }

        ~WinsockRuntime() {
            WSACleanup();
        }
    };
}
#endif

Socket::Socket(int type) {
#ifdef PLATFORM_WINDOWS
    static WinsockRuntime runtime;
    fd_ = WSASocketW(AF_INET, type, 0, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (fd_ == INVALID_SOCKET_FD) {
        LOG_FATAL("socket error: {}", errorString(getLastError()));
    }

    u_long nonblocking = 1;
    if (ioctlsocket(fd_, FIONBIO, &nonblocking) != 0) {
        LOG_FATAL("ioctlsocket error: {}", errorString(getLastError()));
    }
#else
    fd_ = ::socket(AF_INET, type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ == INVALID_SOCKET_FD) {
        LOG_FATAL("socket error: {}", errorString(getLastError()));
    }
#endif
}

Socket::Socket(Socket&& socket) noexcept : fd_(std::exchange(socket.fd_, INVALID_SOCKET_FD)) {}

Socket& Socket::operator=(Socket&& socket) noexcept {
    if (this != &socket) {
        close();
        fd_ = std::exchange(socket.fd_, INVALID_SOCKET_FD);
    }
    return *this;
}

Socket::~Socket() {
    close();
}

void Socket::bind(const Address& address) {
    sockaddr_in addr = address.getAddr();
    if (::bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        LOG_FATAL("bind error, fd: {}, {}", fd_, errorString(getLastError()));
    }
}

void Socket::listen() {
    if (::listen(fd_, SOMAXCONN) != 0) {
        LOG_FATAL("listen error, fd: {}, {}", fd_, errorString(getLastError()));
    }
}

int Socket::connect(const Address& address) const {
    sockaddr_in addr = address.getAddr();
    return ::connect(fd_, (sockaddr*)&addr, sizeof(addr));
}

SocketFd Socket::accept(Address& peerAddress) {
    sockaddr_in addr{};
    SocketLength addrlen = sizeof(addr);
#ifdef PLATFORM_WINDOWS
    SocketFd connfd = ::accept(fd_, (sockaddr*)&addr, &addrlen);
    if (connfd != INVALID_SOCKET_FD) {
        u_long nonblocking = 1;
        if (ioctlsocket(connfd, FIONBIO, &nonblocking) != 0) {
            const int error = getLastError();
            close(connfd);
            WSASetLastError(error);
            return INVALID_SOCKET_FD;
        }
        if (!SetHandleInformation((HANDLE)connfd, HANDLE_FLAG_INHERIT, 0)) {
            const int error = static_cast<int>(GetLastError());
            close(connfd);
            WSASetLastError(error);
            return INVALID_SOCKET_FD;
        }
    }
#else
    SocketFd connfd = ::accept4(fd_, (sockaddr*)&addr, &addrlen, SOCK_CLOEXEC | SOCK_NONBLOCK);
#endif
    if (connfd != INVALID_SOCKET_FD) {
        peerAddress.setAddr(addr);
    }
    return connfd;
}

std::ptrdiff_t Socket::send(const char* data, size_t size) const {
#ifdef PLATFORM_WINDOWS
    const int count = static_cast<int>(std::min(size, static_cast<size_t>(std::numeric_limits<int>::max())));
    return ::send(fd_, data, count, 0);
#else
    return ::send(fd_, data, size, MSG_NOSIGNAL);
#endif
}

std::ptrdiff_t Socket::receive(char* data, size_t size) const {
#ifdef PLATFORM_WINDOWS
    const int count = static_cast<int>(std::min(size, static_cast<size_t>(std::numeric_limits<int>::max())));
    return ::recv(fd_, data, count, 0);
#else
    return ::recv(fd_, data, size, 0);
#endif
}

void Socket::shutdown() const {
#ifdef PLATFORM_WINDOWS
    constexpr int HOW = SD_SEND;
#else
    constexpr int HOW = SHUT_WR;
#endif
    if (::shutdown(fd_, HOW) != 0) {
        LOG_ERROR("shutdown error: {}", errorString(getLastError()));
    }
}

void Socket::close() {
    close(std::exchange(fd_, INVALID_SOCKET_FD));
}

void Socket::setTcpNoDelay(bool on) const {
    setOption(IPPROTO_TCP, TCP_NODELAY, on);
}

void Socket::setReUseAddr(bool on) const {
    setOption(SOL_SOCKET, SO_REUSEADDR, on);
}

void Socket::setReUsePort(bool on) const {
#ifdef PLATFORM_LINUX
    setOption(SOL_SOCKET, SO_REUSEPORT, on);
#else
    (void)on; // Winsock 没有与 SO_REUSEPORT 等价的选项
#endif
}

void Socket::setKeepAlive(bool on) const {
    setOption(SOL_SOCKET, SO_KEEPALIVE, on);
}

int Socket::getLastError() {
#ifdef PLATFORM_WINDOWS
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::string Socket::errorString(int error) {
#ifdef PLATFORM_WINDOWS
    return std::system_category().message(error);
#else
    return std::strerror(error);
#endif
}

bool Socket::isInterrupted(int error) {
#ifdef PLATFORM_WINDOWS
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

bool Socket::isWouldBlock(int error) {
#ifdef PLATFORM_WINDOWS
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

bool Socket::isConnectInProgress(int error) {
#ifdef PLATFORM_WINDOWS
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
    return error == EINPROGRESS || error == EALREADY || error == EINTR;
#endif
}

int Socket::getError(SocketFd fd) {
    int error = 0;
    SocketLength length = sizeof(error);
#ifdef PLATFORM_WINDOWS
    const int result = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &length);
#else
    const int result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
#endif
    return result == 0 ? error : getLastError();
}

bool Socket::getLocalAddress(SocketFd fd, Address& address) {
    sockaddr_in addr{};
    SocketLength length = sizeof(addr);
    if (getsockname(fd, (sockaddr*)&addr, &length) != 0) {
        return false;
    }
    address.setAddr(addr);
    return true;
}

void Socket::close(SocketFd fd) {
    if (fd == INVALID_SOCKET_FD) {
        return;
    }
#ifdef PLATFORM_WINDOWS
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void Socket::setOption(int level, int option, bool on) const {
    const int value = on ? 1 : 0;
#ifdef PLATFORM_WINDOWS
    const int result = setsockopt(fd_, level, option, (const char*)&value, sizeof(value));
#else
    const int result = setsockopt(fd_, level, option, &value, sizeof(value));
#endif
    if (result != 0) {
        LOG_ERROR("setsockopt error, fd: {}, option: {}, {}", fd_, option, errorString(getLastError()));
    }
}
