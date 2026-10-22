//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamNet/address.h>

#include <cstddef>

namespace Dream::detail {
    class Socket {
    public:
        Socket() = default;
        explicit Socket(int type);
        Socket(const Socket& socket) = delete;
        Socket(Socket&& socket) noexcept;
        Socket& operator=(Socket&& socket) noexcept;
        ~Socket();

        void setFd(SocketFd fd) {
            fd_ = fd;
        }

        [[nodiscard]] SocketFd getFd() const {
            return fd_;
        }

        void bind(const Address& address);
        void listen();
        [[nodiscard]] int connect(const Address& address) const;
        [[nodiscard]] SocketFd accept(Address& peerAddress);
        [[nodiscard]] std::ptrdiff_t send(const char* data, size_t size) const;
        [[nodiscard]] std::ptrdiff_t receive(char* data, size_t size) const;
        void shutdown() const;
        void close();

        void setTcpNoDelay(bool on) const;
        void setReUseAddr(bool on) const;
        void setReUsePort(bool on) const;
        void setKeepAlive(bool on) const;

        [[nodiscard]] static int getLastError();
        [[nodiscard]] static std::string errorString(int error);
        [[nodiscard]] static bool isInterrupted(int error);
        [[nodiscard]] static bool isWouldBlock(int error);
        [[nodiscard]] static bool isConnectInProgress(int error);
        [[nodiscard]] static int getError(SocketFd fd);
        [[nodiscard]] static bool getLocalAddress(SocketFd fd, Address& address);
        static void close(SocketFd fd);

    private:
        void setOption(int level, int option, bool on) const;

    private:
        SocketFd fd_ = INVALID_SOCKET_FD;
    };
}
