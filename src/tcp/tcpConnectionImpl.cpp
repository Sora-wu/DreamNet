//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/tcpConnectionImpl.h>
#include <tcp/eventLoopImpl.h>
#include <common/log.hpp>

#include "DreamNet/tcpConnection.h"

using namespace Dream::detail;

namespace {
    constexpr uint32_t BUFFER_SIZE = 1024 * 64;
    constexpr uint32_t HIGH_WATER_MARK_SIZE = 1024 * 1024 * 8; // 8M
}

TcpConnectionImpl::TcpConnectionImpl(TcpConnection* conn, EventLoopImpl* loop, const std::string& name, SocketFd fd,
                                     const Address& localAddress, const Address& remoteAddress) :
    conn_(conn),
    name_(name),
    loop_(loop),
    localAddress_(localAddress),
    remoteAddress_(remoteAddress),
    channel_(std::make_unique<Channel>(loop, fd)),
    inBuffer_(BUFFER_SIZE), outBuffer_(BUFFER_SIZE) {
    socket_.setFd(fd);
}

Dream::Address TcpConnectionImpl::getLocalAddress() const {
    return localAddress_;
}

Dream::Address TcpConnectionImpl::getRemoteAddress() const {
    return remoteAddress_;
}

std::string TcpConnectionImpl::getName() const {
    return name_;
}

void TcpConnectionImpl::connectEstablished() {
    state_ = ConnectionState::CONNECTED;

    channel_->tie(conn_->shared_from_this());
    channel_->setOnReadEvent([this] { handleRead(); });
    channel_->setOnWriteEvent([this] { handleWrite(); });
    channel_->setOnCloseEvent([this] { handleClose(); });
    channel_->setOnErrorEvent([this] { handleError(); });
    channel_->enableReading();
    channel_->enableET();

    if (connectionCallback_) {
        connectionCallback_(conn_->shared_from_this());
    }
}

void TcpConnectionImpl::connectDestroyed() {
    state_ = ConnectionState::DISCONNECTED;
    channel_->disableAll();
}

bool TcpConnectionImpl::isConnected() const {
    return state_ == ConnectionState::CONNECTED;
}

void TcpConnectionImpl::send(const Buffer& buffer, std::shared_ptr<const TcpConnection> self) {
    loop_->runInLoop([this, self = std::move(self), buf = std::move(buffer)]() mutable {
        (void)self; // 仅持有owner引用，保证this在functor执行前不被析构，下同
        sendInLoop(buf);
    });
}

void TcpConnectionImpl::send(const std::span<char>& data, std::shared_ptr<const TcpConnection> self) {
    Buffer buf;
    buf.write(data);

    loop_->runInLoop([this, self = std::move(self), buf = std::move(buf)]() mutable {
        (void)self;
        sendInLoop(buf);
    });
}

void TcpConnectionImpl::send(const char* data, size_t size, std::shared_ptr<const TcpConnection> self) {
    std::span buffer(data, size);
    Buffer buf;
    buf.write(buffer);

    loop_->runInLoop([this, self = std::move(self), buf = std::move(buf)]() mutable {
        (void)self;
        sendInLoop(buf);
    });
}

void TcpConnectionImpl::shutdown(std::shared_ptr<const TcpConnection> self) {
    loop_->runInLoop([this, self = std::move(self)] {
        (void)self;
        if (state_ == ConnectionState::CONNECTED) {
            state_ = ConnectionState::DISCONNECTING;
            if (!channel_->isWriting()) {
                socket_.shutdown();
            }
            else {
                shutdownAfterWrite_ = true;
            }
        }
    });
}

void TcpConnectionImpl::close(std::shared_ptr<const TcpConnection> self) {
    loop_->runInLoop([this, self = std::move(self)] {
        (void)self;
        if (state_ == ConnectionState::CONNECTED) {
            handleClose();
            socket_.close();
        }
    });
}

void TcpConnectionImpl::setConnectionCallback(ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
}

void TcpConnectionImpl::setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
}

void TcpConnectionImpl::setCloseCallback(CloseCallback cb) {
    closeCallback_ = std::move(cb);
}

void TcpConnectionImpl::setHighWaterMarkCallback(HighWaterMarkCallback cb) {
    highWaterMarkCallback_ = std::move(cb);
}

void TcpConnectionImpl::setWriteCompleteCallback(WriteCompleteCallback cb) {
    writeCompleteCallback_ = std::move(cb);
}

void TcpConnectionImpl::sendInLoop(Buffer& buffer) {
    if (state_ != ConnectionState::CONNECTED) {
        return;
    }

    if (!channel_->isWriting()) {
        const std::span<const char> data = buffer.peek();
        std::ptrdiff_t n;
        do {
            n = socket_.send(data.data(), data.size());
        } while (n < 0 && Socket::isInterrupted(Socket::getLastError()));
        if (n > 0) {
            buffer.consume(n);
        }
        else if (n < 0 && !Socket::isWouldBlock(Socket::getLastError())) {
            LOG_ERROR("write error: {}", Socket::errorString(Socket::getLastError()));
            handleClose();
            return;
        }
    }

    if (buffer.readableSize() > 0) {
        if (outBuffer_.readableSize() < HIGH_WATER_MARK_SIZE && outBuffer_.readableSize() + buffer.readableSize() >=
            HIGH_WATER_MARK_SIZE) {
            // 触发高水位预警
            LOG_WARN("trigger highWaterMark!");
            TcpConnectionPtr conn = conn_->shared_from_this();
            if (highWaterMarkCallback_) {
                highWaterMarkCallback_(conn);
            }
            shutdown(conn);
            return;
        }

        outBuffer_.append(buffer);
        buffer.consume(buffer.readableSize()); // 消费源 buffer，避免下次重复发送
        if (!channel_->isWriting()) {
            channel_->enableWriting();
            // 立即再尝试一次写，抵消 ET 注册竞态
            // 如果内核发送缓冲恰好在 write() 返回 EAGAIN 之后、epoll_ctl() 之前这段极小窗口内被对端排空，
            // 那么注册 EPOLLOUT 时 fd 已经可写，ET 认为「没有新的边沿」，就不会再上报
            // 所以这里再试一次
            if (outBuffer_.readableSize() > 0) {
                handleWrite();
            }
        }
    }
}

void TcpConnectionImpl::handleRead() {
    char buff[BUFFER_SIZE]{};
    while (true) {
        const std::ptrdiff_t n = socket_.receive(buff, BUFFER_SIZE);
        if (n == 0) {
            handleClose();
            return;
        }
        if (n < 0) {
            const int error = Socket::getLastError();
            if (Socket::isInterrupted(error)) {
                continue;
            }
            if (Socket::isWouldBlock(error)) {
                break;
            }

            handleError();
            return;
        }

        inBuffer_.write(std::span(buff, n));
        if (messageCallback_) {
            const uint32_t consume = messageCallback_(conn_->shared_from_this(), inBuffer_);
            inBuffer_.consume(consume);
            if (state_ == ConnectionState::DISCONNECTED) {
                return;
            }

            if (inBuffer_.readableSize() > HIGH_WATER_MARK_SIZE) {
                TcpConnectionPtr conn = conn_->shared_from_this();
                if (highWaterMarkCallback_) {
                    highWaterMarkCallback_(conn);
                }
                close(conn_->shared_from_this());
                return;
            }
        }
    }
}

void TcpConnectionImpl::handleWrite() {
    if (!channel_->isWriting()) {
        LOG_ERROR("this connection {} is disable writing!", socket_.getFd());
        return;
    }

    while (outBuffer_.readableSize() > 0) {
        const std::span<const char> data = outBuffer_.peek();
        const std::ptrdiff_t n = socket_.send(data.data(), data.size());
        if (n < 0) {
            const int error = Socket::getLastError();
            if (Socket::isInterrupted(error)) {
                continue;
            }
            if (Socket::isWouldBlock(error)) {
                return; // 内核发送缓冲满，保持写使能，等待下次可写事件
            }
            LOG_ERROR("write error: {}", Socket::errorString(error));
            handleClose();
            return;
        }

        outBuffer_.consume(n);
    }

    if (writeCompleteCallback_) {
        writeCompleteCallback_(conn_->shared_from_this());
    }

    if (shutdownAfterWrite_) {
        shutdownAfterWrite_ = false;
        socket_.shutdown();
    }

    channel_->disableWriting();
}

void TcpConnectionImpl::handleClose() {
    if (state_ == ConnectionState::DISCONNECTED) {
        return;
    }

    LOG_INFO("peer closed connection");
    state_ = ConnectionState::DISCONNECTED;
    channel_->disableAll();

    if (closeCallback_) {
        closeCallback_(conn_->shared_from_this());
    }
}

void TcpConnectionImpl::handleError() {
    if (state_ == ConnectionState::DISCONNECTED) {
        return;
    }

    const int error = Socket::getError(socket_.getFd());
    LOG_ERROR("client error: {}", Socket::errorString(error));
    handleClose();
}
