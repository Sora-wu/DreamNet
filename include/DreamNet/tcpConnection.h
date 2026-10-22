//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamNet/callbacks.h>
#include <DreamNet/socketTypes.h>

#include <memory>
#include <string>

namespace Dream {
    class DREAM_NET_API EventLoop;
    class DREAM_NET_API Address;

    namespace detail {
        class TcpConnectionImpl;
    }

    class DREAM_NET_API TcpConnection : public std::enable_shared_from_this<TcpConnection> {
    public:
        TcpConnection(EventLoop* loop, const std::string& name, SocketFd fd, const Address& localAddress, const Address& remoteAddress);
        ~TcpConnection();
        TcpConnection(const TcpConnection&) = delete;
        TcpConnection& operator=(const TcpConnection&) = delete;

        [[nodiscard]] EventLoop* getLoop() const;
        [[nodiscard]] Address getLocalAddress() const;
        [[nodiscard]] Address getRemoteAddress() const;

        [[nodiscard]] std::string getName() const;
        void connectEstablished() const;
        void connectDestroyed() const;
        [[nodiscard]] bool isConnected() const;
        void send(const Buffer& buffer) const;
        void send(const std::span<char>& buffer) const;
        void send(const char* data, size_t size) const;
        void shutdown() const;
        void close() const;

        void setConnectionCallback(ConnectionCallback cb) const;
        void setMessageCallback(MessageCallback cb) const;
        void setCloseCallback(CloseCallback cb) const;
        void setHighWaterMarkCallback(HighWaterMarkCallback cb) const;
        void setWriteCompleteCallback(WriteCompleteCallback cb) const;

    private:
        std::unique_ptr<detail::TcpConnectionImpl> impl_;
        EventLoop* loop_ = nullptr;
    };
}
